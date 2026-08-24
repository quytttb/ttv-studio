# Jobs layer tests — state machine and durable registry.
add_ttv_studio_test(
    NAME    test_job_state_machine
    SOURCES test_job_state_machine.cpp
    LIBS    jobs
)
add_ttv_studio_test(
    NAME    test_job_store
    SOURCES test_job_store.cpp
    LIBS    jobs
)
