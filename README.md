# SimpleQemuManager

The Project SimpleQemuManager is a very simple demo for controling qemu process.

Because of some issues, I can't use libvirt to manage qemu-kvm. It's so weird that libvirt daemon launched the qemu proccess in differnet weird environt(maybe different sessioin) which caused that the gstreamer would configure its context failed. I have try lots of ways but could not fix it. So I write this demo for control qemu launch and the status of running.

Yeah, it's just for myself now. If anyone know how to resolved the problem, please contact with me! If not, you can try this demo, but just remeber modify the source code. I write some environt arguments in constant, :) ~sorry.
