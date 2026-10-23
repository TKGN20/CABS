use std::collections::HashSet;
use std::env;
use std::fs::{self,File};
use std::io::{BufWriter,Write};
use std::time::Instant;
use seismic::{PlainInvertedIndex};
use seismic::configurations::{Configuration,PruningStrategy,BlockingStrategy,ClusteringAlgorithm,SummarizationStrategy};
use vectorium::{Dataset,DatasetGrowable,DotProduct,PlainSparseDataset,PlainSparseDatasetGrowable,PlainSparseQuantizer,SparseVectorView};

type Data=PlainSparseDataset<u32,f32,DotProduct>;
fn read_u64(bytes:&[u8],offset:usize)->usize {usize::try_from(u64::from_le_bytes(bytes[offset..offset+8].try_into().unwrap())).unwrap()}
fn csr(path:&str)->(Data,Vec<usize>){
    let bytes=fs::read(path).expect("Cannot read original CSR");assert!(bytes.len()>=32,"Truncated CSR");
    let n=read_u64(&bytes,0);let d=read_u64(&bytes,8);let nnz=read_u64(&bytes,16);
    let expected=24usize.checked_add(8usize.checked_mul(n.checked_add(1).unwrap()).unwrap()).unwrap().checked_add(8usize.checked_mul(nnz).unwrap()).unwrap();
    assert_eq!(bytes.len(),expected,"CSR size mismatch");assert!(d>0 && d<=u32::MAX as usize);
    let offsets:Vec<usize>=(0..=n).map(|i|read_u64(&bytes,24+8*i)).collect();assert_eq!(offsets[0],0);assert_eq!(offsets[n],nnz);
    assert!(offsets.windows(2).all(|w|w[0]<=w[1]));
    let ix=24+8*(n+1);let val=ix+4*nnz;
    let mut dataset=PlainSparseDatasetGrowable::new(PlainSparseQuantizer::<u32,f32,DotProduct>::new(d,d));
    for i in 0..n{
        let components:Vec<u32>=(offsets[i]..offsets[i+1]).map(|p|u32::from_le_bytes(bytes[ix+4*p..ix+4*p+4].try_into().unwrap())).collect();
        let values:Vec<f32>=(offsets[i]..offsets[i+1]).map(|p|f32::from_le_bytes(bytes[val+4*p..val+4*p+4].try_into().unwrap())).collect();
        assert!(!components.is_empty() && components.iter().all(|&c|(c as usize)<d) && components.windows(2).all(|w|w[0]<w[1]),"Invalid/empty sparse row");
        assert!(values.iter().all(|v|v.is_finite() && *v>=0.0) && values.iter().any(|v|*v>0.0),"Invalid values");
        dataset.push(SparseVectorView::new(&components,&values));
    }
    (dataset.into(),offsets)
}
fn score(a:SparseVectorView<'_,u32,f32>,b:SparseVectorView<'_,u32,f32>)->f64{
    let mut i=0;let mut j=0;let mut total=0.0;
    while i<a.components().len() && j<b.components().len(){
        if a.components()[i]==b.components()[j]{total+=(a.values()[i] as f64)*(b.values()[j] as f64);i+=1;j+=1;}
        else if a.components()[i]<b.components()[j]{i+=1;}else{j+=1;}
    }total
}
fn main()->Result<(),Box<dyn std::error::Error>>{
    let args:Vec<String>=env::args().collect();assert_eq!(args.len(),13,"Expected data queries measured_ids out seed k repeats postings centroid_fraction summary_energy query_cut heap_factor");
    let seed:u64=args[5].parse()?;assert_eq!(env::var("CABS_BENCH_INDEX_SEED")?.parse::<u64>()?,seed,"Set independent index seed in environment");
    let k:usize=args[6].parse()?;let repeats:usize=args[7].parse()?;let postings:usize=args[8].parse()?;let centroids:f32=args[9].parse()?;let energy:f32=args[10].parse()?;let cut:usize=args[11].parse()?;let factor:f32=args[12].parse()?;
    assert!(k>0 && repeats>0 && postings>0 && cut>0 && centroids.is_finite() && centroids>0.0 && centroids<=1.0 && energy.is_finite() && energy>0.0 && energy<=1.0 && factor.is_finite() && factor>0.0);
    assert_eq!(env::var("RAYON_NUM_THREADS")?,"1","Single-thread baseline fixture required");
    let start=Instant::now();let (documents,offsets)=csr(&args[1]);let (queries,_)=csr(&args[2]);assert_eq!(documents.input_dim(),queries.input_dim());assert!(k<=documents.len());
    let ids:Vec<u64>=fs::read_to_string(&args[3])?.split_whitespace().map(|v|v.parse().expect("Invalid original query ID")).collect();let unique:HashSet<u64>=ids.iter().copied().collect();assert!(!ids.is_empty() && unique.len()==ids.len() && ids.iter().all(|v|*v<queries.len() as u64));
    let config=Configuration::default().pruning_strategy(PruningStrategy::GlobalThreshold{n_postings:postings,max_fraction:1.5}).blocking_strategy(BlockingStrategy::RandomKmeans{centroid_fraction:centroids,min_cluster_size:2,clustering_algorithm:ClusteringAlgorithm::RandomKmeansInvertedIndexApprox{doc_cut:15}}).summarization_strategy(SummarizationStrategy::EnergyPreserving{summary_energy:energy});
    let index=PlainInvertedIndex::<u32,f32>::build(documents,config);let build=start.elapsed().as_secs_f64();
    let search=|qid:u64|{
        let q=queries.get(qid);let (_native,starts)=index.search_with_candidates(q,k,cut,factor,0,false);
        let mut hits:Vec<(u64,f64)>=starts.into_iter().map(|start|{let id=offsets.binary_search(&start).expect("Unknown native document offset") as u64;(id,score(index.dataset().get(id),q))}).collect();
        hits.sort_by(|a,b|b.1.total_cmp(&a.1).then(a.0.cmp(&b.0)));assert!(hits.len()>=k,"SEISMIC underfilled candidates");hits.truncate(k);hits
    };
    for &qid in ids.iter().take(5){
        let q=queries.get(qid);let a=index.search(q,k,cut,factor,0,false);let (b,starts)=index.search_with_candidates(q,k,cut,factor,0,false);assert_eq!(a,b,"Native/candidate path mismatch");
        let candidates:HashSet<u64>=starts.iter().map(|s|offsets.binary_search(s).unwrap() as u64).collect();assert!(a.iter().all(|h|candidates.contains(&h.vector)));search(qid);
    }
    let mut results=BufWriter::new(File::create(format!("{}/retrieved.csv",args[4]))?);let mut timings=BufWriter::new(File::create(format!("{}/queries.csv",args[4]))?);
    writeln!(results,"query_id,rank,identifier,score")?;writeln!(timings,"repeat,query_id,seconds")?;
    for repeat in 0..repeats{for &qid in &ids{let start=Instant::now();let hits=search(qid);let elapsed=start.elapsed().as_secs_f64();writeln!(timings,"{repeat},{qid},{elapsed:.17}")?;if repeat==0{for (rank,(id,value)) in hits.iter().enumerate(){writeln!(results,"{qid},{},{id},{value:.17}",rank+1)?;}}}}
    results.flush()?;timings.flush()?;
    fs::write(format!("{}/summary.json",args[4]),serde_json::to_string_pretty(&serde_json::json!({"method":"SEISMIC","representation":"u32/f32","index_seed":seed,"search_threads":1,"build_seconds_including_loading":build,"exact_binary64_reranking_in_timer":true,"candidate_policy":"upstream visited set; native float pruning and scoring retained","IS_status":"NOT_YET_VALIDATED"}))?)?;
    Ok(())
}
