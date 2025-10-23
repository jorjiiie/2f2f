# list of concerns

- for some reason, in `test.t.cc::basic_multithread_test`, if the thread local buffer is set too small then we'll actually just cause a bus error for some reason?
  - this is concerning because if the buffer gets too big, it should go to the larger resource and just allocate more, which should be `std::pmr::new_delete_resource` which should be thread safe.
  - thus, this having good behavior when we make it a large buffer is quite concerning

- for other mysterious reasons, currently, actually freeing memory will affect delete accuracy. how does this work? it just means things aren't getting deleted properly?
