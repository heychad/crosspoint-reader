# Interface size

Settings > Display > Interface size (restart) selects Small or Large. Large preserves the
existing default. Small uses a smaller body font in shared FreeInk menus and
lists that use the body style, including Settings and Library. Their row
geometry is recalculated from the font. The current book title on Classic and
Lyra Home, and the Lyra Home menu, also follow this setting. Lyra Extended
(Three Covers) shares the scaled menu and empty-state heading, but its book
titles use a fixed font. File Browser labels, reader text, and fixed-size
chrome are unchanged.

The setting is saved with the other settings and takes effect after restart.
Restarting avoids changing touch targets while a screen is being refreshed.
The setting is hidden from the web interface.
The current settings importer does not yet migrate the old CrossInk value.

X4 Pro testing must cover both sizes, touch targets, long titles and non-Latin
text in Settings, Library, File Browser, and Classic and Lyra Home; reopen
settings and reboot to check persistence. Verify unchanged reader text too.
