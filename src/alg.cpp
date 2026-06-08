#include "alg.h"
#include <filesystem>
#include <algorithm>
#include <cctype>


#if defined(unix) || defined(__unix__)
std::string data_fold = "/home/xizhao/dataset/", index_fold = " ";
//std::string data_fold = "./../../wd/dataset/", index_fold = " ";
std::string data_fold1 = data_fold, data_fold2 = data_fold+("MIPS/");
#else
std::string data_fold = "E:/Dataset_for_c/", index_fold = "";
std::string data_fold1 = data_fold, data_fold2 = "E:/Dataset_for_c/MIPS/";

#endif


#if defined(unix) || defined(__unix__)
struct llt
{
	int date, h, m, s;
	llt(size_t diff) { set(diff); }
	void set(size_t diff)
	{
		date = diff / 86400;
		diff = diff % 86400;
		h = diff / 3600;
		diff = diff % 3600;
		m = diff / 60;
		s = diff % 60;
	}
};
#endif

void saveAndShow(float c, int k, std::string& dataset, std::vector<resOutput>& res)
{
	time_t now = time(0);
	tm* ltm = new tm[1];
	localtime_s(ltm, &now);
	// std::string query_result = ("results/Running_result.txt");
	// std::ofstream os(query_result, std::ios_base::app);
	// os.seekp(0, std::ios_base::end); // 

	//time_t now = std::time(0);
	time_t zero_point = 1635153971 - 17 * 3600 - 27 * 60;//Let me set the time at 2021.10.25. 17:27 as the zero point
	size_t diff = (size_t)(now - zero_point);
#if defined(unix) || defined(__unix__)
	llt lt(diff);
#endif

	double date = ((float)(now - zero_point)) / 86400;
	float hour = date - floor(date);
	hour *= 24;
	float minute= hour = date - floor(date);


	std::stringstream ss;

	ss << "*******************************************************************************************************\n"
		<< "The result of Algorithms for " << dataset << " is as follow: c="<<c<<", k="<<k
		<<"\n"
		<< "*******************************************************************************************************\n";

	ss << std::setw(12) << "algName"
		<< std::setw(12) << "c"
		<< std::setw(12) << "k"
		<< std::setw(12) << "m"
		<< std::setw(12) << "l"
		<< std::setw(12) << "Time"
		<< std::setw(12) << "Recall"
		<< std::setw(12) << "Cost"
		<< std::endl
		<< std::endl;
	for (int i = 0; i < res.size(); ++i) {
		ss << std::setw(12) << res[i].algName
			<< std::setw(12) << res[i].ub
			<< std::setw(12) << res[i].k
			<< std::setw(12) << res[i].L
			<< std::setw(12) << res[i].K
			<< std::setw(12) << res[i].time
			<< std::setw(12) << res[i].recall
			<< std::setw(12) << res[i].cost
			<< std::endl;
	}
#if defined(unix) || defined(__unix__)
	ss << "\n******************************************************************************************************\n"
		<< "                                                                                    "
		<< lt.date << '-' << lt.h << ':' << lt.m << ':' << lt.s
		<< "\n******************************************************************************************************\n\n\n";
#else
	ss << "\n******************************************************************************************************\n"
		<< "                                                                                    "
		<< ltm->tm_mon + 1 << '-' << ltm->tm_mday << ' ' << ltm->tm_hour << ':' << ltm->tm_min
		<< "\n*****************************************************************************************************\n\n\n";
#endif
	

	std::cout << ss.str();
// os << ss.str();
	// os.close();  delete []ltm;
	//delete[] ltm;
}

std::string getNameByDate(int mode, const std::string& dataset)
{
        std::string size_tag = "1M";
        std::string ds_lower = dataset;
        std::transform(ds_lower.begin(), ds_lower.end(), ds_lower.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
        if (ds_lower.find("0.1m") != std::string::npos || ds_lower.find("100k") != std::string::npos) size_tag = "0.1M";
        else if (ds_lower.find("10m") != std::string::npos) size_tag = "10M";
        else if (ds_lower.find("1m") != std::string::npos) size_tag = "1M";

        std::string exp_tag = "exp0";
        if (mode == 5) exp_tag = "exp1";
        else if (mode == 6) exp_tag = "exp2";
        else if (mode == 7) exp_tag = "exp3";

        std::string safe_dataset = dataset;
        for (char& ch : safe_dataset) {
                if (!(std::isalnum((unsigned char)ch) || ch == '_' || ch == '-' || ch == '.')) ch = '_';
        }

        std::string family_tag = "SPLADE";
        if (ds_lower.find("splade-v3") != std::string::npos) family_tag = "SPLADE-v3";
        else if (ds_lower.find("e-splade") != std::string::npos) family_tag = "E-SPLADE";
        else if (ds_lower.find("unicoil") != std::string::npos) family_tag = "uniCoil-T5";
        else if (ds_lower.find("yelp") != std::string::npos) family_tag = "Yelp";
        else if (ds_lower.find("randm") != std::string::npos) family_tag = "RANDM";
        else if (ds_lower.find("nq") != std::string::npos) family_tag = "NQ";

        std::filesystem::create_directories(std::string("results/") + family_tag + "/" + size_tag);
        std::string res = std::string("results/") + family_tag + "/" + size_tag + "/" + exp_tag + "_" + safe_dataset + "_";

        time_t now = time(0);
        tm* ltm = new tm[1];
        localtime_s(ltm, &now);
        time_t zero_point = 1635153971 - 17 * 3600 - 27 * 60;
        size_t diff = (size_t)(now - zero_point);
#if defined(unix) || defined(__unix__)
        llt lt(diff);
#endif

#if defined(unix) || defined(__unix__)
        res += std::to_string(lt.date) + "_" + std::to_string(lt.h) + "_" + std::to_string(lt.m) + "_" + std::to_string(lt.s);
#else
        res += std::to_string(ltm->tm_year + 1900) + "_" + std::to_string(ltm->tm_mon + 1) + "_" + std::to_string(ltm->tm_mday)
                + "_" + std::to_string(ltm->tm_hour) + "_" + std::to_string(ltm->tm_min) + "_" + std::to_string(ltm->tm_sec);
#endif
        delete []ltm;
        // res += ".txt";
        return res;
}
