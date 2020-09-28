# osrfx2
<p>Linux kernel module for the <a href="http://osrfx2.sourceforge.net/">osrfx2 device</a>. The device consists of a set of inputs and outputs as a resource for 
getting familar with USB development and kernel programming. More specifically the device includes:</p>

<ul>
  <li>Switch bank</li>
  <li>Bargraph LED</li>
  <li>7-segment LED</li>
</ul>

All the information about the device, including vendor and product information, USB endpoints and control commands can be found in the <a href="http://www.osronline.com/hardware/OSRFX2_35.pdf">datasheet</a>.

<h2>Introduction</h2>

<p>The USB device consists of 3 endpoints plus the control one:</p>

<ul>
  <li>2 bulk enpoints: one in, one out in wich messages sent to the in endpoint are fowarded to the out endpoint as a circular buffer</li>
  <li>1 interrupt endpoint for reporting the status of the switch bank.</li>
</ul>

<h2>Implementation</h2>
<p>The implementation of the driver is capable of detecting the bulk and interrupt endpoints. It implements the capabalities of the bulk (in and out) endpoints through
system calls (read and write) and the interrupt endpoint at kernel level.<p>

<p>The driver has been tested against a real osrfx2 device</p>

<h2>Pending</h2>
<p>To be done:</p>
<ul>
  <li>Provide access from user space to the interrupt either via ioctl system call or via the sysfs</li>
  <li>Implement custom vendor commands</li>
</ul>
