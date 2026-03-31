#include "test_framework.h"
#include "../src/crdt/vector_clock.h"

TEST(vclock_tick) {
    VectorClock vc("node-a");
    ASSERT_EQ(vc.get_local_time(), static_cast<uint64_t>(0));
    uint64_t t1 = vc.tick();
    ASSERT_EQ(t1, static_cast<uint64_t>(1));
    uint64_t t2 = vc.tick();
    ASSERT_EQ(t2, static_cast<uint64_t>(2));
}

TEST(vclock_merge) {
    VectorClock a("node-a");
    VectorClock b("node-b");
    a.tick(); a.tick();
    b.tick();

    a.merge(b);
    ASSERT_EQ(a.get_clock().at("node-a"), static_cast<uint64_t>(2));
    ASSERT_EQ(a.get_clock().at("node-b"), static_cast<uint64_t>(1));
}

TEST(vclock_happens_before) {
    VectorClock a("node-a");
    VectorClock b("node-a");
    a.tick();
    b.tick(); b.tick();

    ASSERT_TRUE(a.happens_before(b));
    ASSERT_FALSE(b.happens_before(a));
}

TEST(vclock_concurrent) {
    VectorClock a("node-a");
    VectorClock b("node-b");
    a.tick();
    b.tick();

    ASSERT_TRUE(a.is_concurrent(b));
    ASSERT_TRUE(b.is_concurrent(a));
}

TEST(vclock_not_concurrent_after_merge) {
    VectorClock a("node-a");
    VectorClock b("node-b");
    a.tick();
    b.tick();
    b.merge(a);
    b.tick();

    ASSERT_TRUE(a.happens_before(b));
    ASSERT_FALSE(a.is_concurrent(b));
}

TEST(vclock_serialize_roundtrip) {
    VectorClock vc("node-a");
    vc.tick(); vc.tick(); vc.tick();

    VectorClock other("node-b");
    other.tick();
    vc.merge(other);

    auto bytes = vc.serialize();
    auto vc2 = VectorClock::deserialize(bytes.data(), bytes.size());

    ASSERT_EQ(vc2.get_node_id(), std::string("node-a"));
    ASSERT_EQ(vc2.get_clock().at("node-a"), static_cast<uint64_t>(3));
    ASSERT_EQ(vc2.get_clock().at("node-b"), static_cast<uint64_t>(1));
}

TEST(vclock_equal_not_happens_before) {
    VectorClock a("node-a");
    VectorClock b("node-a");
    a.tick();
    b.tick();

    ASSERT_FALSE(a.happens_before(b));
    ASSERT_FALSE(b.happens_before(a));
}
