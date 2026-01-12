#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string_view>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

// 16.64s user 0.36s system 93% cpu 18.090 total
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



void update_stats(Entry* table, std::string_view name, int16_t temp, uint64_t hash) {

  uint64_t idx = hash % TABLE_SIZE;

  while (table[idx].occupied && table[idx].name != name) {
	idx = (idx + 1) % TABLE_SIZE;
  }

  if (!table[idx].occupied) {
	table[idx].occupied = true;
	table[idx].name = name;
	table[idx].stats = {temp, temp, temp, 1};
  } 
  else {
	
	if (temp < table[idx].stats.min) table[idx].stats.min = temp;
	if (temp > table[idx].stats.max) table[idx].stats.max = temp;
	table[idx].stats.sum += temp;
	table[idx].stats.cnt++;
  }
  
}



int main() {

  //unsigned int num_threads = std::thread::hardware_concurrency();
  //if (num_threads == 0)
    //num_threads = 4;

 
  Entry table[TABLE_SIZE];

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

  char *cursor = file_ptr;
  char *end = file_ptr + file_size;
  
  while (cursor < end) {
    if (*cursor == '\n' || *cursor == '\r' || *cursor == ' ') {
      cursor++;
      continue;
    }

	uint64_t hash = 0;
    char *semicolon = cursor;
    while (*semicolon != ';') {
	  hash = (hash * 31) + static_cast<unsigned char>(*semicolon);
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
      if (*curr == '-') negative = true;
      else if (*curr >= '0' && *curr <= '9') {
        temp = temp * 10 + (*curr - '0');
      }
    }
    if (negative) temp = -temp;

	update_stats(table, city, temp, hash);
    cursor = newline + 1;
  }

  int active_count = 0;
  for (int i = 0; i < TABLE_SIZE; i++) {
	if (table[i].occupied){
	  table[active_count++] = table[i];
	}
  }

  std::sort(table, table + active_count, [](const Entry& a, const Entry& b) {
    return a.name < b.name;
  });

  std::cout << std::fixed << std::setprecision(1) << "{";

  for (int i = 0; i < active_count; ++i) {
    const auto name = table[i].name;
    const auto s = table[i].stats;
    std::cout << name << "=" << s.min / 10.0f << "/" << (s.sum / static_cast<float>(s.cnt)) / 10.0f
              << "/" << s.max / 10.0f
              << (i == active_count - 1 ? "" : ",");
  }
  std::cout << "}\n";
  return 0;
}
