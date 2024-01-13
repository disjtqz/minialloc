#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <list>
#include <set>
#include <cassert>
#include <intrin.h>
#include "../minialloc.hpp"

static const char g_chartable[] = "abcdefghijklmnopqrstuvwxyz";

static std::string create_random_string() {
    std::string result{ };

    uint16_t length;
    do {
        _rdrand16_step(&length);
        length &= 0x1f;
    } while (length == 0);
    for (unsigned i = 0; i < length; ++i) {
        uint16_t currchar = 0;
        _rdrand16_step(&currchar);
        result.push_back(g_chartable[currchar % (sizeof(g_chartable) - 1)]);
    }
    return result;
}


int main() {
    uint8_t* memory_pool_data = new uint8_t[2 * 1024 * 1024];
    memset(memory_pool_data, 0, 2 * 1024 * 1024);
    allocator_t my_allocator{ memory_pool_data, 2 * 1024 * 1024, 2048 };

    my_allocator.assert_is_in_initial_state();
    my_allocator.validate_nodepool();

    std::list<const char*> current_strings{};
    std::set<size_t> lengths_allocated{};
    uint16_t random_count = 0;
    do {
        _rdrand16_step(&random_count);

        random_count %= 2048;
    } while (random_count == 0);



    auto add_n_random_strings = [&my_allocator, &current_strings,&lengths_allocated](uint32_t random_count) {

        for (uint32_t random_string_index = 0; random_string_index < random_count; ++random_string_index) {
            auto rand_str = create_random_string();

            uint32_t length = strlen(rand_str.data()) + 1;
            
            lengths_allocated.insert(length);
            void* str_alloced = my_allocator.allocate(length);

            memcpy(str_alloced, rand_str.c_str(), length);
            current_strings.push_back((const char*)str_alloced);
        }
    };

    auto arbitrary_free_strings = [&my_allocator, &current_strings](bool free_all=false) {

        uint32_t num_nodes_freed = 0;
        for (auto node_iter = current_strings.begin(); node_iter != current_strings.end(); ) {

            uint16_t random_choice = 0;
            _rdrand16_step(&random_choice);

            if ((random_choice & 1) || free_all) {
                my_allocator.deallocate((void*)*node_iter,
                    strlen(*node_iter) + 1);

                auto node_to_erase = node_iter;
                ++node_iter;
                current_strings.erase(node_to_erase);
                ++num_nodes_freed;
            }
            else {
                ++node_iter;
            }
        }
        return num_nodes_freed;
    };

    my_allocator.validate_freelist();
    add_n_random_strings(random_count);
    my_allocator.validate_freelist();
    my_allocator.validate_nodepool();



    uint32_t num_nodes_freed = arbitrary_free_strings();
    my_allocator.validate_freelist();
    my_allocator.validate_nodepool();

    add_n_random_strings(num_nodes_freed);
    my_allocator.validate_freelist();
    my_allocator.validate_nodepool();

    //free all strings
    arbitrary_free_strings(true);
    my_allocator.validate_nodepool();
    my_allocator.assert_is_in_initial_state();

    return 0;


}