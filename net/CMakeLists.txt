fugue_module(fugue_net)
# fugue_text is a STATIC library, so its objects reach consumers transitively (object libraries do not).
target_link_libraries(fugue_net PUBLIC fugue_text)
