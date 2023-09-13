// linecounter.cpp : Defines the entry point for the application.
//

#include "linecounter.h"

#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

namespace
{
	std::uint64_t countLinesInFile(const std::filesystem::path& path)
	{
		std::ifstream input(path, std::ifstream::ate);
		if (input.tellg() == 0)
		{
			return 0;
		}
		input.seekg(0);
		return std::count(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>(), '\n') + 1;
	}
}

class SharedStorage
{
public:
	void put(const std::filesystem::path& path)
	{
		std::lock_guard<std::mutex> lock(mutex);
		files_to_process.push_back(path);
	}

	std::optional<std::filesystem::path> take()
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (!files_to_process.empty())
		{
			auto file = files_to_process.front();
			files_to_process.pop_front();
			return file;
		}
		return std::nullopt;
	}

private:
	std::mutex mutex;
	std::deque<std::filesystem::path> files_to_process;
};

class LineCounter
{
public:
	std::uint64_t countLines(const std::filesystem::path& root)
	{
		reset();

		std::vector<std::thread> workers;
		for (unsigned int i = 0; i < std::thread::hardware_concurrency(); ++i)
		{
			workers.emplace_back([this]() { runWorker(); });
		}

		std::thread skanner([this, root]() { scanDirectoryTree(root); });

		skanner.join();
		for (auto& worker : workers)
		{
			worker.join();
		}

		return line_count;
	}

private:

	void reset()
	{
		line_count = 0;
		scanning_finished = false;
	}

	void scanDirectoryTree(const std::filesystem::path& root)
	{
		for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
		{
			if (entry.is_regular_file())
			{
				storage.put(entry.path());
			}
		}
		scanning_finished = true;
	}

	void runWorker()
	{
		while (true)
		{
			auto file = storage.take();
			if (file)
			{
				line_count += countLinesInFile(*file);
			}
			else if (scanning_finished)
			{
				break;
			}
		}
	}

	SharedStorage storage;
	std::atomic_uint64_t line_count = 0;
	std::atomic_bool scanning_finished = false;
};

int main(int argc, char* argv[])
{
	auto path = std::filesystem::current_path();
	if (argc == 2)
		path = std::filesystem::absolute(argv[1]);

	LineCounter counter;

	auto start = std::chrono::system_clock::now();

	auto linesCount = counter.countLines(path);

	std::chrono::duration<double> elapsed_seconds = std::chrono::system_clock::now() - start;

	std::cout << "elapsed time: " << elapsed_seconds << std::endl;
	std::cout << linesCount << std::endl;

	return 0;
}
