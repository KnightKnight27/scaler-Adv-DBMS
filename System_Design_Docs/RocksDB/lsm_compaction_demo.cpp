#include <iostream>
#include <numeric>
#include <vector>

struct LsmLevel
{
    int id;
    int files;
    int recordsPerFile;
};

int main()
{
    std::vector<LsmLevel> storageLevels{
        {0, 4, 1000},
        {1, 6, 4000},
        {2, 8, 12000}
    };

    int logicalRecords = 0;
    int physicalRecordsTouched = 0;

    for (const auto& level : storageLevels)
    {
        const int levelRecords = level.files * level.recordsPerFile;
        logicalRecords += levelRecords;

        const int rewriteMultiplier = level.id + 1;
        physicalRecordsTouched += levelRecords * rewriteMultiplier;

        std::cout << "L" << level.id
                  << " files=" << level.files
                  << " records=" << levelRecords
                  << " rewrite_factor=" << rewriteMultiplier
                  << '\n';
    }

    const double amplification =
        static_cast<double>(physicalRecordsTouched) / static_cast<double>(logicalRecords);

    std::cout << "Logical records: " << logicalRecords << '\n';
    std::cout << "Physical records touched during compaction estimate: "
              << physicalRecordsTouched << '\n';
    std::cout << "Estimated write amplification: " << amplification << "x\n";

    return 0;
}
