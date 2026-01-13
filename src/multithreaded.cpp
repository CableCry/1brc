//
//  Benchmark 1: ./multithreaded
//    Time (mean ± σ):      1.037 s ±  0.011 s    [User: 24.107 s, System: 0.679
//    Range (min … max):    1.015 s …  1.054 s    10 runs

#include <algorithm>
#include <cstdint>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <string.h>
#include <string_view>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

constexpr int TABLE_SIZE = 16384;

// Packed stats into just entry and made cnt smaller,
// now at 32 bytes we can fit 2 entrys in a cache line
// shaved of ~0.02 secs
struct Entry {
  std::string_view name;
  int64_t sum;
  int32_t cnt;
  int16_t min;
  int16_t max;
};

struct ThreadResult {
  Entry table[TABLE_SIZE] = {};
};

void update_(Entry *table, std::string_view name, int16_t temp, uint64_t hash) {

  uint64_t idx = hash % TABLE_SIZE;
  while (table[idx].name.data() != nullptr && table[idx].name != name) {
    idx = (idx + 1) % TABLE_SIZE;
  }

  if (!(table[idx].name.data() != nullptr)) {
    table[idx] = {name, temp, temp, temp, 1};
  }

  else {
    if (temp < table[idx].min)
      table[idx].min = temp;

    if (temp > table[idx].max)
      table[idx].max = temp;

    table[idx].sum += temp;
    table[idx].cnt++;
  }
}

void process_chunk(char *start, char *end, ThreadResult &result) {
  char *cursor = start;
  while (cursor < end) {

    if (*cursor == '\n' || *cursor == '\r' || *cursor == ' ') {
      cursor++;
      continue;
    }

    uint64_t hash{0};
    char *name_start = cursor;

    while (*cursor != ';') {
      hash = (hash * 31) + static_cast<unsigned char>(*cursor);
      cursor++;
    }

    // This didnt actually help and increased time by 0.1 secs
    // try to take out the branching and vectorize the search for ;
    // char *semicolon = static_cast<char *>(memchr(cursor, ';', end - cursor));
    // size_t length = semicolon - cursor;
    // for (size_t i = 0; i < length; ++i) {
    //  hash = (hash * 31) + static_cast<unsigned char>(cursor[i]);
    //}

    std::string_view city(name_start, cursor - name_start);

    int16_t sign = 1;
    if (*cursor == '-') {
      sign = -1;
      cursor++;
    }

    // We know that the format is either N.N or NN.N so we can remove some
    // branching
    int16_t temp{0};
    if (cursor[1] == '.') {
      temp = (cursor[0] - '0') * 10 + (cursor[2] - '0');
      cursor += 4;
    } else {
      temp =
          (cursor[0] - '0') * 100 + (cursor[1] - '0') * 10 + (cursor[3] - '0');
      cursor += 5;
    }

    temp *= sign;
    update_(result.table, city, temp, hash);
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

  // this is O(n^2) to bring it to the front
  Entry *master = results[0]->table;
  for (size_t i = 1; i < results.size(); ++i) {
    for (int j = 0; j < TABLE_SIZE; ++j) {

      if (results[i]->table[j].name.data() != nullptr) {

        std::string_view name = results[i]->table[j].name;
        uint64_t hash{0};

        for (char c : name)
          hash = (hash * 31) + static_cast<unsigned char>(c);

        uint64_t idx = hash % TABLE_SIZE;

        while (master[idx].name.data() != nullptr && master[idx].name != name) {
          idx = (idx + 1) % TABLE_SIZE;
        }

        if (!(master[idx].name.data() != nullptr)) {
          master[idx] = results[i]->table[j];
        }

        else {
          if (results[i]->table[j].min < master[idx].min)
            master[idx].min = results[i]->table[j].min;

          if (results[i]->table[j].max > master[idx].max)
            master[idx].max = results[i]->table[j].max;

          master[idx].sum += results[i]->table[j].sum;
          master[idx].cnt += results[i]->table[j].cnt;
        }
      }
    }
  }

  int active_count = 0;
  for (int i = 0; i < TABLE_SIZE; ++i) {
    if (master[i].name.data() != nullptr) {
      master[active_count] = master[i];
      active_count++;
    }
  }

  std::sort(master, master + active_count,
            [](const Entry &a, const Entry &b) { return a.name < b.name; });

  std::cout << std::fixed << std::setprecision(1) << "{";
  for (int i = 0; i < active_count; ++i) {
    const auto &s = master[i];
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
