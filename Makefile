.PHONY: all test sanitize clean
all:
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
	cmake --build build -j
test: all
	ctest --test-dir build --output-on-failure
sanitize:
	cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug -DCABS_SANITIZE=ON
	cmake --build build-sanitize -j
	ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-sanitize --output-on-failure
clean:
	cmake --build build --target clean
