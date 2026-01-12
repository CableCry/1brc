#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <iomanip>
#include <vector>

// ./base_line  123.03s user 6.24s system 99% cpu 2:09.74 total


struct Stats {
    double min = 100.0;
    double max = -100.0;
    double sum = 0.0;
    long count = 0;
};

int main() {
    std::ifstream file("../1brc/measurements.txt");
    std::map<std::string, Stats> results;
    std::string line;

    while (std::getline(file, line)) {
        size_t sep = line.find(';');
        std::string city = line.substr(0, sep);
        double temp = std::stod(line.substr(sep + 1));

        auto& s = results[city];
        if (temp < s.min) s.min = temp;
        if (temp > s.max) s.max = temp;
        s.sum += temp;
        s.count++;
    }

    std::cout << std::fixed << std::setprecision(1) << "{";
    for (auto it = results.begin(); it != results.end(); ++it) {
        const auto& [name, s] = *it;
        std::cout << name << "=" << s.min << "/" << (s.sum / s.count) << "/" << s.max
                  << (std::next(it) == results.end() ? "" : ",");
    }
    std::cout << "}" << std::endl;

    return 0;
}
