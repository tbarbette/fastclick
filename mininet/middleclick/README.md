# Mininet example for middleclick-tcp-ids.click

Use `vagrant up` to build a pre-configured VM with all dependencies. Of course, vagrant and virtualbox must be installed themselves.

Then, run `vagrant ssh` to jump into the VM.

Then, simply run the Mininet topology with:

```
sudo mn -c ; cd /vagrant && sudo python2 topology.py
```

If in a separate window (you have to leave mininet running) you run `sudo ~/mininet/util/m h1 curl http://10.221.0.5/` you should see the default nginx welcome page.

Now, let's play a little with the IDS. Kill mininet and restart it with those parameters:
```
sudo mn -c ; sudo python topology.py word=nginx.com mode=MASK all=1
```
This will look for "nginx.com" and replace all characters of all occurences by asterisks.

If you run again the curl command, you should see the occurences of nginx.com changed:
```
<a href="http://*********/">*********</a>.</p>
```

With `mode=ALERT all=0`, you will only receive an alert in the log that you can check with:

```
cd ~/middleclick/ &&  tail -f click.log
```

With `mode=REPLACE all=1 pattern=nginx.org", you will replace all occurences of nginx.com per nginx.com:
```
<a href="http://nginx.org/">nginx.org</a>.<br/>
```

Check conf/middleclick/middleclick-tcp-ids.click for the other modes, HTTP support to remove/replace/insert content, even with different sizes, etc...
