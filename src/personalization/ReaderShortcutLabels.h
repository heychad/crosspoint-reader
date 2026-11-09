#pragma once
#include <I18n.h>

#include <iterator>

#include "ReaderShortcuts.h"
namespace readershortcuts {
inline constexpr StrId ACTION_LABELS[] = {StrId::STR_SHORTCUT_DEFAULT,
                                          StrId::STR_DISABLED,
                                          StrId::STR_SHORTCUT_PREV_PAGE,
                                          StrId::STR_SHORTCUT_NEXT_PAGE,
                                          StrId::STR_SHORTCUT_PREV_CHAPTER,
                                          StrId::STR_SHORTCUT_NEXT_CHAPTER,
                                          StrId::STR_READER_MENU,
                                          StrId::STR_BOOKMARK_OPTION,
                                          StrId::STR_DICTIONARY,
                                          StrId::STR_FOOTNOTES,
                                          StrId::STR_FORCE_REFRESH,
                                          StrId::STR_TOGGLE_FRONTLIGHT,
                                          StrId::STR_SHORTCUT_BRIGHTER,
                                          StrId::STR_SHORTCUT_DIMMER,
                                          StrId::STR_SHORTCUT_ROTATE_CW,
                                          StrId::STR_SHORTCUT_ROTATE_CCW,
                                          StrId::STR_SHORTCUT_WARMER,
                                          StrId::STR_SHORTCUT_COOLER,
                                          StrId::STR_SHORTCUT_LARGER_TEXT,
                                          StrId::STR_SHORTCUT_SMALLER_TEXT,
                                          StrId::STR_SHORTCUT_ROTATE_HALF,
                                          StrId::STR_QUICK_ACTIONS,
                                          StrId::STR_HOME_SHORTCUT,
                                          StrId::STR_BROWSE_FILES,
                                          StrId::STR_LIBRARY,
                                          StrId::STR_READING_STATISTICS,
                                          StrId::STR_SCREENSHOT_BUTTON,
                                          StrId::STR_KOSYNC,
                                          StrId::STR_SAVE_CLIPPING,
                                          StrId::STR_NIGHT_MODE,
                                          StrId::STR_FOCUS_READING,
                                          StrId::STR_TOUCH_READER_CONTROLS,
                                          StrId::STR_TILT_PAGE_TURN,
                                          StrId::STR_CYCLE_AUTO_TURN,
                                          StrId::STR_QUICK_LOCK,
                                          StrId::STR_SLEEP,
                                          StrId::STR_SAVED_ITEMS,
                                          StrId::STR_FILE_TRANSFER};
static_assert(std::size(ACTION_LABELS) == static_cast<unsigned>(Action::Count));
inline StrId label(Action action) {
  const auto index = static_cast<unsigned>(action);
  return index < std::size(ACTION_LABELS) ? ACTION_LABELS[index] : StrId::STR_NONE_OPT;
}
}  // namespace readershortcuts
