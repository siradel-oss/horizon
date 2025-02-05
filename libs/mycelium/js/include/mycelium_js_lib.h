extern "C"
{
    extern unsigned int my_js_begin_query_elapsed_time();
    extern void my_js_end_time_query();
    extern double my_js_query_time_elapsed(int id);
    extern void my_js_delete_query(int id);

    extern unsigned int my_js_check_program_link_completion_status(int id);
}
