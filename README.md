# tmon: a tiny system monitor for linux
![tmon](img/tmon.png)

## Screenshots
### tmon in st
![tmon-st](img/tmon-st.png)

### tmon in tty mode
![tmon-tty](img/tmon-tty.png)

## How to tmon
- Install and type `tmon`
- Toggle each bar with keybinds
- Space bar to toggle unicode, juust in case anyone wants to use it on tty
- "H" key for help and keybinds
- "Q" to quit

## Dependencies
- a linux machine
### Build dependencies
- ncurses
### Runtime dependencies
- `sensors`

## Installation
### Build & install
```
git clone https://github.com/pondda/tmon.git
cd tmon
make && sudo make install
```
### Uninstall
```
sudo make uninstall
```

## Troubleshooting
Not seeing a temperature reading? Try running
```
sudo sensors-detect
```
You can also change the TEMP command in the source code and replace `Core 0` with the sensor of your choice!

## Licence
GNU General Public License, version 3 (GPL-3.0)
