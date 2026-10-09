#include "test.hpp"

#include "engine/scheduler.hpp"

using namespace gcs;

static Scheduler fresh() {
    Scheduler s;
    for (size_t i = 0; i < Scheduler::N; ++i) s.set_load(static_cast<Lane>(i), {});
    return s;
}

static WorkItem parallel_work(u64 n, bool bit_identical = false,
                              Cls cls = Cls::Display) {
    WorkItem w;
    w.shape                 = Shape::DataParallel;
    w.items                 = n;
    w.result_class          = cls;
    w.requires_bit_identity = bit_identical;
    return w;
}

TEST(sched_small_work_stays_on_cpu) {
    // GPU dispatch costs ~60 us; below the crossover launching is pure loss.
    const auto d = fresh().schedule(parallel_work(100));
    CHECK(d.lane == Lane::CpuScalar);
}

TEST(sched_large_work_goes_to_gpu) {
    const auto d = fresh().schedule(parallel_work(10'000'000));
    CHECK(d.lane == Lane::GpuCompute);
}

TEST(sched_busy_cpu_pushes_work_to_gpu) {
    Scheduler s = fresh();
    const u64 n = 50'000;      // just under the idle crossover (~60k)

    CHECK(s.schedule(parallel_work(n)).lane == Lane::CpuScalar);

    s.set_load(Lane::CpuScalar, {0.95, 8, 1.0});
    const auto d = s.schedule(parallel_work(n));
    CHECK_MSG(d.lane == Lane::GpuCompute,
              "under CPU load the work stayed on %s", lane_name(d.lane));
}

TEST(sched_busy_gpu_pulls_work_back_to_cpu) {
    Scheduler s = fresh();
    const u64 n = 2'000'000;   // ~30x past the crossover; GPU wins when idle

    CHECK(s.schedule(parallel_work(n)).lane == Lane::GpuCompute);

    s.set_load(Lane::GpuCompute, {0.9, 40, 1.0});
    const auto d = s.schedule(parallel_work(n));
    CHECK_MSG(d.lane == Lane::CpuScalar,
              "under GPU load the work stayed on %s", lane_name(d.lane));
}

TEST(sched_thermal_throttle_moves_work_off_the_gpu) {
    Scheduler s = fresh();
    const u64 n = 500'000;
    CHECK(s.schedule(parallel_work(n)).lane == Lane::GpuCompute);

    s.set_load(Lane::GpuCompute, {0.5, 0, 0.02});   // hard thermal throttle
    CHECK(s.schedule(parallel_work(n)).lane == Lane::CpuScalar);
}

TEST(sched_matrix_work_prefers_the_cpu_matrix_unit) {
    WorkItem w;
    w.shape = Shape::Matrix;
    w.items = 50'000;
    const auto d = fresh().schedule(w);
    CHECK(d.lane == Lane::CpuMatrix);
    CHECK(d.reject_reason(Lane::GpuCompute) == Reject::WrongShape);
}

TEST(sched_irregular_work_never_leaves_the_cpu) {
    WorkItem w;
    w.shape = Shape::Irregular;
    w.items = 100'000'000;
    const auto d = fresh().schedule(w);
    CHECK(d.lane == Lane::CpuScalar);
    CHECK(d.reject_reason(Lane::GpuCompute) == Reject::WrongShape);
}

TEST(sched_bit_identical_work_never_lands_on_a_non_reproducible_lane) {
    WorkItem w;
    w.shape                 = Shape::Matrix;
    w.items                 = 10'000'000;
    w.result_class          = Cls::Authoritative;
    w.requires_bit_identity = true;

    const auto d = fresh().schedule(w);
    CHECK_MSG(d.lane != Lane::CpuMatrix,
              "bit-identical work was scheduled onto a non-reproducible lane");
    CHECK(d.lane == Lane::CpuScalar);
    CHECK(d.reject_reason(Lane::CpuMatrix) == Reject::NotBitReproducible);
}

TEST(sched_bit_identical_work_may_still_use_the_gpu) {
    const auto d = fresh().schedule(parallel_work(10'000'000, true, Cls::Authoritative));
    CHECK(d.lane == Lane::GpuCompute);
    CHECK(d.reject_reason(Lane::GpuCompute) == Reject::None);
}

TEST(sched_scalar_choice_follows_the_workload_not_the_lane) {
    const WorkItem must_match = parallel_work(1000, true, Cls::Authoritative);
    CHECK(Scheduler::scalar_for(must_match, Lane::GpuCompute) ==
          Scheduler::ScalarChoice::SoftF64);

    const WorkItem snapped = parallel_work(1000, false, Cls::Authoritative);
    CHECK(Scheduler::scalar_for(snapped, Lane::GpuCompute) ==
          Scheduler::ScalarChoice::DF64);

    const WorkItem display = parallel_work(1000, false, Cls::Display);
    CHECK(Scheduler::scalar_for(display, Lane::GpuCompute) ==
          Scheduler::ScalarChoice::F32);

    CHECK(Scheduler::scalar_for(display, Lane::CpuScalar) ==
          Scheduler::ScalarChoice::F64);
}

TEST(sched_locking_overrides_load_entirely) {
    Scheduler s = fresh();

    s.lock(Shape::DataParallel, Lane::CpuScalar);
    const auto d = s.schedule(parallel_work(100'000'000));
    CHECK(d.lane == Lane::CpuScalar);
    CHECK(d.locked);
    CHECK(d.reject_reason(Lane::GpuCompute) == Reject::LockedElsewhere);

    s.unlock(Shape::DataParallel);
    CHECK(s.schedule(parallel_work(100'000'000)).lane == Lane::GpuCompute);
}

TEST(sched_lock_all_gives_ci_a_deterministic_pipeline) {
    Scheduler s = fresh();
    s.lock_all(Lane::CpuScalar);

    CHECK(s.schedule(parallel_work(100'000'000)).lane == Lane::CpuScalar);

    WorkItem m;
    m.shape = Shape::Matrix;
    m.items = 10'000'000;
    CHECK(s.schedule(m).lane == Lane::CpuScalar);
}

TEST(sched_unavailable_lane_is_skipped) {
    Scheduler s = fresh();
    s.set_caps(Lane::GpuCompute, {false, true});     // no GPU on this machine
    const auto d = s.schedule(parallel_work(100'000'000));
    CHECK(d.lane == Lane::CpuScalar);
    CHECK(d.reject_reason(Lane::GpuCompute) == Reject::Unavailable);
}

TEST(sched_always_produces_a_runnable_lane) {
    Scheduler s = fresh();
    s.set_caps(Lane::GpuCompute, {false, true});
    s.set_caps(Lane::CpuMatrix, {false, false});

    for (Shape sh : {Shape::DataParallel, Shape::Matrix, Shape::Irregular, Shape::Serial}) {
        WorkItem w;
        w.shape = sh;
        w.items = 1000;
        CHECK(s.schedule(w).lane == Lane::CpuScalar);
    }
}

TEST(sched_decisions_are_explainable) {
    WorkItem w;
    w.shape                 = Shape::Matrix;
    w.items                 = 1'000'000;
    w.requires_bit_identity = true;

    const auto d = fresh().schedule(w);
    CHECK(d.reject_reason(Lane::GpuCompute) == Reject::WrongShape);
    CHECK(d.reject_reason(Lane::CpuMatrix) == Reject::NotBitReproducible);
    CHECK(d.reject_reason(Lane::CpuScalar) == Reject::None);
    CHECK(d.estimated_ns > 0.0);
}
