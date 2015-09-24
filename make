unset exit

set link false
set vncview false
set vncsession false
set vncdisplay false
set colortables false
set rezfork false

clearmem

newer vncview.a vncview.cc
if {status} != 0
	set vncview true
	set link true
        end

newer vncsession.a vncsession.cc
if {status} != 0
	set vncsession true
        set link true
end

newer vncview.a vncview.h
if {status} != 0
	set vncview true
	set link true
end

newer vncsession.a vncview.h
if {status} != 0
	set vncsession true
        set link true
end

newer vncsession.a vncsession.h
if {status} != 0
	set vncsession true
        set link true
end

newer vncview.a vncsession.h
if {status} != 0
	set vncview true
	set link true
end

newer vncdisplay.a vncdisplay.cc
if {status} != 0
	set vncdisplay true
	set link true
end

newer colortables.a colortables.cc
if {status} != 0
	set colortables true
        set link true
end

newer vncview.rezfork vncview.rez
if {status} != 0
	set rezfork true
end

set exit on

if {rezfork} == true
	compile vncview.rez keep=vncview.rezfork
        copy -C -P -R vncview.rezfork VNCview.GS
end
if {vncview} == true
	compile +O vncview.cc keep=vncview
end
if {vncsession} == true
	compile +O vncsession.cc keep=vncsession
end
if {vncdisplay} == true
	compile +O vncdisplay.cc keep=vncdisplay
end
if {colortables} == true
	compile +O colortables.cc keep=colortables
end
if {link} == true
	link vncview vncsession vncdisplay colortables keep=VNCview.GS
	filetype VNCview.GS S16 $DB03
end
