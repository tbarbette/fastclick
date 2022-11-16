# Mininet examples for FastClick configurations

Use `vagrant up` to build a pre-configured VM with all dependencies. Of course, vagrant and virtualbox must be installed themselves.

Then, run `vagrant ssh` to jump into the VM.

Then, simply run the Mininet topology with:

```
sudo mn -c ; cd /vagrant && sudo python2 EXAMPLE/topology.py
```

EXAMPLE is one of the folder in this repository:
 * switch: A switch between two hosts (there is also a topology-mininetonly.py file that does the same switch without Click, for learning purposes)
 * router: A router between 4 hosts TODO

The examples use the click configuration in the conf/EXAMPLE/ folders, just tweaking a few variables through the command line.
