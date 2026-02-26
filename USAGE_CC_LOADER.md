How to use cc_loader.sh

1) Switch to root 
su -
2) Make the script executable
chmod +x cc_loader.sh

4) Load a congestion control module
./cc_loader.sh cc_<algorithm_name>

Example:
./cc_loader.sh cc_vegas

If you want to load AND switch to the new algorithm
./cc_loader.sh -s cc_<algorithm_name>

Example:
./cc_loader.sh -s cc_vegas
