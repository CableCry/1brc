#include <cstdint>
#include <iomanip>
#include <iostream>
#include <map>
#include <string_view>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>

// 81.34s user 0.43s system 100% cpu 1:21.06 total

struct Stats {
  int16_t min = 1000;
  int16_t max = -1000;
  int32_t sum = 0;
  int64_t cnt = 0;
};

using namespace std::chrono;

int main() {

  // unsigned int num_threads = std::thread::hardware_concurrency();
  // if (num_threads == 0)
  // num_threads = 4;

  auto file_time_s = high_resolution_clock::now();

  int fd = open("../1brc/measurements.txt", O_RDONLY);
  struct stat sb;
  fstat(fd, &sb);
  size_t file_size = sb.st_size;

  char *file_ptr =
      static_cast<char *>(mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0));
  if (file_ptr == MAP_FAILED) {
    std::cout << "Could not map file \n";
    return 1;
  }
  madvise(file_ptr, file_size, MADV_SEQUENTIAL | MADV_WILLNEED);

  std::map<std::string_view, Stats> results;
  char *cursor = file_ptr;
  char *end = file_ptr + file_size;

  auto file_time_e = high_resolution_clock::now();
  auto file_time = duration_cast<microseconds>(file_time_e - file_time_s);

  auto parse_time_s = high_resolution_clock::now();
  while (cursor < end) {

    char *semicolon = cursor;
    while (*semicolon != ';') {
      semicolon++;
    }

    char *newline = semicolon + 1;
    while (*newline != '\n') {
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

    auto &s = results[city];
    if (temp < s.min)
      s.min = temp;
    if (temp > s.max)
      s.max = temp;
    s.sum += temp;
    s.cnt++;

    cursor = newline + 1;
  }

  auto parse_time_e = high_resolution_clock::now();
  auto parse_time = duration_cast<microseconds>(parse_time_e - parse_time_s);

  std::cout << std::fixed << std::setprecision(1) << "{";
  for (auto it = results.begin(); it != results.end(); ++it) {
    const auto &[name, s] = *it;
    std::cout << name << "=" << s.min / 10.0f << "/"
              << (s.sum / static_cast<float>(s.cnt)) / 10.0f << "/"
              << s.max / 10.0f << (std::next(it) == results.end() ? "" : ",");
  }
  std::cout << "}\n";

  auto total_time = file_time.count() + parse_time.count();

  std::cout << "File Time: " << file_time.count() << "us" << "\n";
  std::cout << "Parse Time: " << parse_time.count() << "us" << "\n";
  return 0;
}
