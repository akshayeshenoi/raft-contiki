To run sim in 'headless' mode, we must write a 'plugin script' in the simulation
environment.

Steps:
- Open the simulation in GUI mode
- Go to `Tools > Simulation Editor`
- Add the following lines
```
TIMEOUT(72000000); // 20 hours

while (true) {
  log.log(time + ":" + id + ":" + msg + "\n");
  YIELD();
}
```
- Save the simulation
- Run as
```
ant run_nogui -Dargs=<path-to-sim>
```