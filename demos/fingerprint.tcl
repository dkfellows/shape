#!/bin/sh

# This little demo (in the splendid tradition of xteddy and xbomb :^)
# is due to John LoVerso <loverso@opengroup.org> and I have adapted it
# a little in order to increase its effectiveness.

# Now we make cunning use of the backslash/shell trick \
[ -x `dirname $0`/../shapewish ] && exec `dirname $0`/../shapewish $0 ${1+"$@"} || exec wish $0 ${1+"$@"} || { echo "`basename $0`: couldn't start wish" >&2 ; exit 1; }

set dir [file join [pwd] [file dirname [info script]] .]
package ifneeded Shape 0.4 "package require Tk 8\n\
        [list tclPkgSetup "$dir/../unix/" Shape 0.4 {{libshape04.so.1.0 load shape}}]"
lappend auto_path [file join $dir ..]
package require Shape

set images [file join $dir images]

. config -bg tan -cursor dotbox
wm overrideredirect . 1
wm withdraw .
update idle
shape set . -bound bitmap @[file join $images fingerprint.xbm]
wm deiconify .
bind . <1> {
    regexp {\+(-?[0-9]+)\+(-?[0-9]+)} [winfo geometry .] dummy x y
    set x [expr $x-%X]
    set y [expr $y-%Y]
}
bind . <B1-Motion> {
    wm geometry . +[expr %X+$x]+[expr %Y+$y]
}
