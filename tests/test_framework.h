#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <stdexcept>

struct TestCase {
    std::string name;
    std::function<void()> func;
};

std::vector<TestCase>& get_tests();
int register_test(const std::string& name, std::function<void()> func);

#define ASSERT_TRUE(cond) do { if (!(cond)) throw std::runtime_error( \
    std::string(__FILE__) + ":" + std::to_string(__LINE__) + " ASSERT_TRUE failed: " #cond); } while(0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

#define ASSERT_EQ(a, b) do { if ((a) != (b)) throw std::runtime_error( \
    std::string(__FILE__) + ":" + std::to_string(__LINE__) + " ASSERT_EQ failed: " #a " != " #b); } while(0)

#define ASSERT_NE(a, b) do { if ((a) == (b)) throw std::runtime_error( \
    std::string(__FILE__) + ":" + std::to_string(__LINE__) + " ASSERT_NE failed: " #a " == " #b); } while(0)

#define ASSERT_GT(a, b) do { if (!((a) > (b))) throw std::runtime_error( \
    std::string(__FILE__) + ":" + std::to_string(__LINE__) + " ASSERT_GT failed: " #a " <= " #b); } while(0)

#define TEST(name) \
    static void test_##name(); \
    static int _reg_##name = register_test(#name, test_##name); \
    static void test_##name()
