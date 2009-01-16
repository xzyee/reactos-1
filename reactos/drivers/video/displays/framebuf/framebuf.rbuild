<?xml version="1.0"?>
<!DOCTYPE module SYSTEM "../../../../tools/rbuild/project.dtd">
<module name="framebuf" type="kernelmodedll" entrypoint="DrvEnableDriver@12" installbase="system32" installname="framebuf.dll" crt="libcntpr">
	<importlibrary definition="framebuf.spec" />
	<include base="framebuf">.</include>
        <if property="LUSER" value="1">
          <define name="LUSER"/>
          <library>luser</library>
        </if>
	<library>win32k</library>
        <library>libcntpr</library>
	<file>enable.c</file>
	<file>palette.c</file>
	<file>pointer.c</file>
	<file>screen.c</file>
	<file>surface.c</file>
	<file>framebuf.rc</file>
</module>
