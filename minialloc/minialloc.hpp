#pragma once


using allocation_displacement_t = ptrdiff_t;

struct allocation_node_t {
    allocation_displacement_t m_base;
    allocation_displacement_t m_size;
    allocation_displacement_t m_next_node;
    allocation_displacement_t m_previous_node;
};

static constexpr allocation_displacement_t k_bad_displacement = 0;
struct allocator_t {
private:
    //nodes that represent a region of free memory
    allocation_displacement_t m_first_allocation_node;
    //nodes that are not currently associated with any free memory, but are available to
    //represent future chunks of free memory
    allocation_displacement_t m_first_pooled_node;
    uint8_t* m_memory;
    size_t m_total_memory_size;

    size_t m_max_allocations;

    template<typename T>
    T* translate_displacement(allocation_displacement_t displacement) {
        return reinterpret_cast<T*>(&m_memory[displacement]);
    }

    allocation_node_t* translate_node(allocation_displacement_t displacement) {
        return translate_displacement<allocation_node_t>(displacement);
    }

    template<typename T>
    allocation_displacement_t convert_to_displacement(T* ptr) {
        return reinterpret_cast<uint8_t*>(ptr) - m_memory;
    }
public:
    allocator_t(uint8_t* mem, size_t total_memory_size, size_t max_allocations) : m_first_allocation_node(k_bad_displacement), m_first_pooled_node(k_bad_displacement), m_memory(mem), m_total_memory_size(total_memory_size), m_max_allocations(max_allocations) {

        auto sizeof_nodes = (sizeof(allocation_node_t) * (max_allocations + 1));

        size_t size_after_nodes = total_memory_size - sizeof_nodes;

        auto nodes = translate_node(sizeof(allocation_node_t)); //skip offset 0, our invalid offset

        nodes[0].m_base = sizeof_nodes;
        nodes[0].m_size = size_after_nodes;
        nodes[0].m_next_node = k_bad_displacement;
        nodes[0].m_previous_node = k_bad_displacement;

        m_first_allocation_node = convert_to_displacement(&nodes[0]);


        for (size_t pooled_node_index = 0; pooled_node_index < max_allocations - 1; ++pooled_node_index) {

            auto current_node = &nodes[pooled_node_index + 1];

            current_node->m_base = k_bad_displacement;
            current_node->m_size = 0;

            if (pooled_node_index != (max_allocations - 2)) {
                current_node->m_next_node = convert_to_displacement(&current_node[1]);
            }
            else {
                current_node->m_next_node = k_bad_displacement;
            }

            if (pooled_node_index != 0) {
                current_node->m_previous_node = convert_to_displacement(&current_node[-1]);
            }
            else {
                current_node->m_previous_node = k_bad_displacement;
            }
        }

        m_first_pooled_node = convert_to_displacement(&nodes[1]);
    }

private:

    allocation_node_t* new_node_from_pool() {
        if (m_first_pooled_node != k_bad_displacement) {
            allocation_node_t* result = translate_node(m_first_pooled_node);
            assert_pooled_node_correct(result);

            m_first_pooled_node = result->m_next_node;
            if (m_first_pooled_node != k_bad_displacement) {
                translate_node(m_first_pooled_node)->m_previous_node = k_bad_displacement;
            }
            result->m_next_node = k_bad_displacement;
            result->m_previous_node = k_bad_displacement;
            return result;
        }
        else {
            assert(false);
            return nullptr;
        }
    }

    void release_node_to_pool(allocation_node_t* node) {
        auto node_displ = convert_to_displacement(node);
        node->m_base = k_bad_displacement;
        node->m_size = 0;
        node->m_next_node = m_first_pooled_node;
        node->m_previous_node = k_bad_displacement;

        if (m_first_pooled_node != k_bad_displacement) {
            translate_node(m_first_pooled_node)->m_previous_node = node_displ;
        }
        m_first_pooled_node = node_displ;
        assert_pooled_node_correct(node);

    }

    void append_allocation_to_front(allocation_displacement_t allocation_base, size_t allocation_size) {

        if (m_first_allocation_node != k_bad_displacement) {
            auto first_node = translate_node(m_first_allocation_node);
            //the allocation we are freeing + its size forms a contiguous region with the allocation
            //at the front of the alloc list, adjust the fronts base address and size to contain this allocation
            if (first_node->m_base == allocation_base + allocation_size) {
                first_node->m_base = allocation_base;
                first_node->m_size += allocation_size;
                assert_allocation_node_correct(first_node);
                return;
            }
        }

        auto new_node = new_node_from_pool();
        new_node->m_base = allocation_base;
        new_node->m_size = allocation_size;
        new_node->m_next_node = m_first_allocation_node;
        new_node->m_previous_node = k_bad_displacement;

        if (m_first_allocation_node != k_bad_displacement) {
            translate_node(m_first_allocation_node)->m_previous_node = convert_to_displacement(new_node);
        }
        m_first_allocation_node = convert_to_displacement(new_node);
        assert_allocation_node_correct(new_node);

    }

    void insert_allocation_to_tail(allocation_node_t* tail_node, allocation_displacement_t allocation_base, size_t allocation_size) {

        if ((tail_node->m_base + tail_node->m_size) == allocation_base) {
            tail_node->m_size += allocation_size;
            assert_allocation_node_correct(tail_node);
            return;
        }

        auto new_node = new_node_from_pool();

        new_node->m_base = allocation_base;
        new_node->m_size = allocation_size;

        new_node->m_next_node = k_bad_displacement;
        new_node->m_previous_node = convert_to_displacement(tail_node);
        tail_node->m_next_node = convert_to_displacement(new_node);
        assert_allocation_node_correct(tail_node);
        assert_allocation_node_correct(new_node);
    }

    void insert_allocation_between(allocation_node_t* first, allocation_node_t* second, allocation_displacement_t allocation_base, size_t allocation_size) {

        if ((first->m_base + first->m_size) == allocation_base) {

            //freeing this allocation causes first and second to form a contiguous region!
            if ((allocation_base + allocation_size) == second->m_base) {

                first->m_size += allocation_size;
                first->m_size += second->m_size;


                //erase second from the list
                first->m_next_node = second->m_next_node;


                if (first->m_next_node != k_bad_displacement) {
                    translate_node(first->m_next_node)->m_previous_node = convert_to_displacement(first);
                }

                release_node_to_pool(second);
                assert_allocation_node_correct(first);
                return;

            }

            first->m_size += allocation_size;
            assert_allocation_node_correct(first);
            return;

        }

        else if ((allocation_base + allocation_size) == second->m_base) {
            second->m_base = allocation_base;
            assert_allocation_node_correct(second);
            return;
        }
        else {
            auto new_node = new_node_from_pool();

            new_node->m_base = allocation_base;
            new_node->m_size = allocation_size;
            new_node->m_previous_node = convert_to_displacement(first);
            new_node->m_next_node = convert_to_displacement(second);
            first->m_next_node = convert_to_displacement(new_node);
            second->m_previous_node = convert_to_displacement(new_node);
            assert_allocation_node_correct(new_node);
            return;
        }

    }
public:
    void* allocate(size_t allocation_size) {
        allocation_node_t* current_node = nullptr;
        for (allocation_displacement_t node_displacement = m_first_allocation_node; node_displacement != k_bad_displacement; node_displacement = current_node->m_next_node) {
            current_node = translate_node(node_displacement);
            if (current_node->m_size >= allocation_size) {

                if (current_node->m_size != allocation_size) {
                    //shrink the node
                    auto result_displacement = current_node->m_base;
                    current_node->m_base += allocation_size;
                    current_node->m_size -= allocation_size;
                    validate_freelist();
                    return translate_displacement<uint8_t>(result_displacement);

                }
                else {
                    //exact match!

                    if (node_displacement != m_first_allocation_node) {
                        puts("Allocate: Exact match not front");
                        auto result_displacement = current_node->m_base;

                        if (current_node->m_next_node != k_bad_displacement) {
                            translate_node(current_node->m_next_node)->m_previous_node = current_node->m_previous_node;
                        }
                        translate_node(current_node->m_previous_node)->m_next_node = current_node->m_next_node;

                        release_node_to_pool(current_node);
                        validate_freelist();
                        return translate_displacement<uint8_t>(result_displacement);
                    }
                    else {
                        puts("Allocate: Exact match front");
                        //exact match on the front of the list
                        m_first_allocation_node = current_node->m_next_node;
                        if (m_first_allocation_node != k_bad_displacement) {
                            translate_node(m_first_allocation_node)->m_previous_node = k_bad_displacement;

                        }
                        auto result_displacement = current_node->m_base;
                        release_node_to_pool(current_node);
                        validate_freelist();
                        return translate_displacement<uint8_t>(result_displacement);
                    }
                }

            }
        }
        assert(false);
        return nullptr;
    }


    void deallocate(void* memory, size_t allocation_size) {

        auto mem_displacement = convert_to_displacement(memory);

        auto previous_node = k_bad_displacement;

        auto current_node = m_first_allocation_node;

        while (current_node != k_bad_displacement) {
            auto translated_node = translate_node(current_node);

            if (translated_node->m_base > mem_displacement) {
                break;
            }

            previous_node = current_node;
            current_node = translated_node->m_next_node;

        }

        if (previous_node == k_bad_displacement) {
            puts("Deallocate: Append to front");
            append_allocation_to_front(mem_displacement, allocation_size);
        }
        else if (current_node == k_bad_displacement) {
            puts("Deallocate: Append to tail");

            insert_allocation_to_tail(translate_node(previous_node), mem_displacement, allocation_size);
        }
        else {
            puts("Deallocate: Insert between");
            insert_allocation_between(translate_node(previous_node), translate_node(current_node), mem_displacement, allocation_size);
        }
        validate_freelist();
    }

    void assert_allocation_node_correct(allocation_node_t* node) {

        auto displacement = convert_to_displacement(node);

        assert(displacement >= sizeof(allocation_node_t));
        assert((displacement % sizeof(allocation_node_t)) == 0);
        if (node->m_previous_node == k_bad_displacement) {
            assert(m_first_allocation_node == displacement);
        }
        else {
            auto prev = translate_node(node->m_previous_node);
            assert(prev->m_next_node == displacement);
            assert((prev->m_base + prev->m_size) < node->m_base);
        }

        if (node->m_next_node != k_bad_displacement) {
            auto next = translate_node(node->m_next_node);

            assert(next->m_previous_node == displacement);

            auto node_base_psize = node->m_base + node->m_size;
            assert(next->m_base > node_base_psize);
        }

        assert(node->m_base + node->m_size <= m_total_memory_size);

        assert(node < translate_node(sizeof(allocation_node_t) * (m_max_allocations + 1)));
    }

    void assert_pooled_node_correct(allocation_node_t* node) {
        auto displacement = convert_to_displacement(node);

        assert(displacement >= sizeof(allocation_node_t));
        assert((displacement % sizeof(allocation_node_t)) == 0);
        if (node->m_previous_node == k_bad_displacement) {
            assert(m_first_pooled_node == displacement);
        }
        else {
            auto prev = translate_node(node->m_previous_node);
            assert(prev->m_next_node == displacement);    
        }

        if (node->m_next_node != k_bad_displacement) {
            auto next = translate_node(node->m_next_node);

            assert(next->m_previous_node == displacement);
        }

        assert(node->m_base == k_bad_displacement);
        assert(node->m_size == 0);
    }

    void assert_is_in_initial_state() {
        dump_allocation_state();
        size_t nodes_in_pool = 0;

        allocation_displacement_t first_pooled = m_first_pooled_node;

        while (first_pooled != k_bad_displacement) {
            ++nodes_in_pool;
            first_pooled = translate_node(first_pooled)->m_next_node;
        }

        assert(nodes_in_pool == m_max_allocations - 1);


        assert(m_first_allocation_node != k_bad_displacement);

        auto first_alloc_node = translate_node(m_first_allocation_node);

        assert(first_alloc_node->m_next_node == k_bad_displacement);
        assert(first_alloc_node->m_previous_node == k_bad_displacement);
        //base must come directly after alloc nodes
        assert(first_alloc_node->m_base == sizeof(allocation_node_t) * (m_max_allocations + 1));

        size_t size_after_nodes = m_total_memory_size - (sizeof(allocation_node_t) * (m_max_allocations + 1));

        assert(first_alloc_node->m_size == size_after_nodes);
    }

    void dump_allocation_state() {
        auto node = m_first_allocation_node;

        while (node != k_bad_displacement) {
            auto xnode = translate_node(node);
            printf("Node at %llX, base = 0x%llX, size = 0x%llX\n", node, xnode->m_base, xnode->m_size);
            node = xnode->m_next_node;
        }
        
    }

    void validate_freelist() {
        auto node = m_first_allocation_node;

        while (node != k_bad_displacement) {
            assert_allocation_node_correct(translate_node(node));
            node = translate_node(node)->m_next_node;
        }
    }

    void validate_nodepool() {
        auto node = m_first_pooled_node;
        while (node != k_bad_displacement) {
            assert_pooled_node_correct(translate_node(node));
            node = translate_node(node)->m_next_node;
        }
    }
};