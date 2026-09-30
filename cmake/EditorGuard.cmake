# ni_require_editor(<plugin dir> <globbed web files...>)
#
# AN EMPTY GLOB IS A PLUGIN WITH NO EDITOR, AND IT USED TO SHIP.
#
# resources/web is BUILD OUTPUT -- vite writes it from ui/, and it is not
# tracked. Each plugin's CMakeLists runs vite at configure time and then globs
# what it wrote into WEB_RESOURCES. The vite step only WARNS when it fails -- a
# fresh clone where nobody ran `npm ci`, a broken lockfile, npm missing
# entirely. The glob then finds nothing, every format builds and links happily,
# and iPlug2's deploy step does `rm -rf` on the installed bundle before copying
# the new one over it. So a failed editor build does not merely produce a bad
# plugin: it REPLACES a working installed plugin with one whose
# Contents/Resources/web does not exist.
#
# What the user sees is a white window. Nothing in the build output says why --
# the warning scrolled past a thousand lines earlier, if it was even this build
# that produced it.
#
# So: refuse to configure. A build that stops with a message naming the fix is
# always better than one that succeeds and uninstalls the editor. This used to
# guard the Trance Gate alone; now that no plugin's resources/web is tracked, a
# fresh clone reaches the empty case for every one of them.
function(ni_require_editor plugin_dir)
    set(web ${ARGN})
    list(FILTER web INCLUDE REGEX "/index\\.html$")
    if (NOT web)
        message(FATAL_ERROR
            "plugins/${plugin_dir}/resources/web has no index.html, so this build "
            "would install a plugin with NO EDITOR over whatever is there now -- "
            "a white window with nothing in the log to explain it.\n"
            "resources/web is build output. Run:  npm ci  (at the repository "
            "root), then configure again.")
    endif()
endfunction()
