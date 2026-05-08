#ifndef PROCESS_HPP
#define PROCESS_HPP

#include <vector>
#include "ProcMap.hpp"

namespace Process
{
    std::vector<ProcMap> get_process_maps(int pid);
    int get_pid_by_name(const char *process_name);
};

#endif // PROCESS_HPP