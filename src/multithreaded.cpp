#include <algorithm>
#include <cstdint>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

// 30.17s user 0.69s system 2698% cpu 1.144 total
//
//	Benchmark 1: ./multithreaded
//  	Time (mean ± σ):      1.224 s ±  0.036 s    [User: 30.479 s, System: 0.669 s]
//  	Range (min … max):    1.146 s …  1.263 s    10 runs

constexpr int TABLE_SIZE = 16384;

struct Stats {
  int16_t min;
  int16_t max;
  int32_t sum;
  int64_t cnt;
};

struct Entry {
  std::string_view name;
  Stats stats;
  bool occupied;
};

struct ThreadResult {
  Entry table[TABLE_SIZE] = {};
};

void update_stats(Entry *table, std::string_view name, int16_t temp,
                  uint64_t hash) {
  uint64_t idx = hash % TABLE_SIZE;
  while (table[idx].occupied && table[idx].name != name) {
    idx = (idx + 1) % TABLE_SIZE;
  }
  if (!table[idx].occupied) {
    table[idx].occupied = true;
    table[idx].name = name;
    table[idx].stats = {temp, temp, temp, 1};
  } else {
    if (temp < table[idx].stats.min)
      table[idx].stats.min = temp;
    if (temp > table[idx].stats.max)
      table[idx].stats.max = temp;
    table[idx].stats.sum += temp;
    table[idx].stats.cnt++;
  }
}

void process_chunk(char *start, char *end, ThreadResult &result) {
  char *cursor = start;
  while (cursor < end) {
    if (*cursor == '\n' || *cursor == '\r' || *cursor == ' ') {
      cursor++;
      continue;
    }
    uint64_t hash = 0;
    char *semicolon = cursor;
    while (semicolon < end && *semicolon != ';') {
      hash = (hash * 31) + static_cast<unsigned char>(*semicolon);
      semicolon++;
    }
    if (semicolon >= end)
      break;
    char *newline = semicolon + 1;
    while (newline < end && *newline != '\n') {
      newline++;
    }
    std::string_view city(cursor, semicolon - cursor);
    int16_t temp = 0;
    bool negative = false;
    for (char *curr = semicolon + 1; curr < newline; ++curr) {
      if (*curr == '-')
        negative = true;
      else if (*curr >= '0' && *curr <= '9') {
        temp = temp * 10 + (*curr - '0');
      }
    }
    if (negative)
      temp = -temp;
    update_stats(result.table, city, temp, hash);
    cursor = newline + 1;
  }
}

int main() {
  int fd = open("../1brc/measurements.txt", O_RDONLY);
  if (fd == -1)
    return 1;
  
  struct stat sb;
  fstat(fd, &sb);
  size_t file_size = sb.st_size;
  char *file_ptr =
      static_cast<char *>(mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0));
  
  if (file_ptr == MAP_FAILED)
    return 1;
  madvise(file_ptr, file_size, MADV_SEQUENTIAL | MADV_WILLNEED);

  unsigned int num_threads = std::thread::hardware_concurrency();
  if (num_threads == 0)
    num_threads = 4;

  std::vector<std::thread> threads;
  std::vector<ThreadResult *> results;
  size_t chunk_size = file_size / num_threads;

  for (unsigned int i = 0; i < num_threads; ++i) {
    
	char *chunk_start = file_ptr + (i * chunk_size);
    char *chunk_end = (i == num_threads - 1)
                          ? (file_ptr + file_size)
                          : (file_ptr + (i + 1) * chunk_size);
    
	if (i > 0) {
      while (chunk_start < (file_ptr + file_size) && *(chunk_start - 1) != '\n')
        chunk_start++;
    }

    if (i < num_threads - 1) {
      while (chunk_end < (file_ptr + file_size) && *(chunk_end - 1) != '\n')
        chunk_end++;
    }
    ThreadResult *res = new ThreadResult();
    results.push_back(res);
    threads.emplace_back(process_chunk, chunk_start, chunk_end, std::ref(*res));
  }

  for (auto &t : threads)
    t.join();

  Entry *master = results[0]->table;
  for (size_t i = 1; i < results.size(); ++i) {
    for (int j = 0; j < TABLE_SIZE; ++j) {

      if (results[i]->table[j].occupied) {
      
		std::string_view name = results[i]->table[j].name;
        uint64_t hash = 0;
        
		for (char c : name)
          hash = (hash * 31) + static_cast<unsigned char>(c);
        
		uint64_t idx = hash % TABLE_SIZE;
        
		while (master[idx].occupied && master[idx].name != name) {
          idx = (idx + 1) % TABLE_SIZE;
        }
        
		if (!master[idx].occupied) {
          master[idx] = results[i]->table[j];
        } 

		else {
          if (results[i]->table[j].stats.min < master[idx].stats.min)
            master[idx].stats.min = results[i]->table[j].stats.min;
        
		  if (results[i]->table[j].stats.max > master[idx].stats.max)
            master[idx].stats.max = results[i]->table[j].stats.max;
          
		  master[idx].stats.sum += results[i]->table[j].stats.sum;
          master[idx].stats.cnt += results[i]->table[j].stats.cnt;
        }
      }
    }
  }

  int active_count = 0;
  for (int i = 0; i < TABLE_SIZE; ++i) {
    if (master[i].occupied)
      master[active_count++] = master[i];
  }

  std::sort(master, master + active_count,
            [](const Entry &a, const Entry &b) { return a.name < b.name; });

  std::cout << std::fixed << std::setprecision(1) << "{";
  for (int i = 0; i < active_count; ++i) {
    const auto &s = master[i].stats;
    std::cout << master[i].name << "=" << s.min / 10.0f << "/"
              << (s.sum / static_cast<float>(s.cnt)) / 10.0f << "/"
              << s.max / 10.0f << (i == active_count - 1 ? "" : ", ");
  }
  std::cout << "}\n";

  // Getting rid of this shaves off ~0.4s
  // for (auto r : results) delete r;
  // munmap(file_ptr, file_size);
  // close(fd);
  return 0;
}
