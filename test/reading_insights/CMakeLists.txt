add_test(NAME ReadingInsightsUi COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_ui.py)

add_test(NAME ReadingInsightsHandoff COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_handoff.py)
