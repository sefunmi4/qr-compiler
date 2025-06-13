#include "../runtime/memory.h"
#include "../runtime/scheduler.h"
#include <iostream>
#include <cassert>
#include "../runtime/random.h"

using namespace qpp;

int main() {
    seed_rng(42);
    int qid = memory.create_qregister(1);
    int result = -1;

    std::vector<std::string> order;
    Task t1{"hadamard", Target::QPU, ExecHint::NONE, 5, [qid,&order]() {
        order.push_back("h");
        memory.qreg(qid).h(0);
    }};
    Task t2{"measure", Target::QPU, ExecHint::NONE, 10, [qid, &result,&order]() {
        order.push_back("m");
        result = memory.qreg(qid).measure(0);
    }};

    scheduler.add_task(t1);
    scheduler.add_task(t2);
    scheduler.run_async();
    scheduler.wait();

    assert(result == 0 || result == 1);
    assert(order.size() == 2);
    assert(order[0] == "m"); // higher priority task first
    memory.release_qregister(qid);

    std::cout << "Scheduler test passed." << std::endl;
    return 0;
}
