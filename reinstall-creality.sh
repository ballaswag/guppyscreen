#!/bin/sh

BACKUP_DIR=/usr/data/guppyify-backup

# bail out of restoring if the backup directory does not exist
# should not kill guppyscreen if we cannot restore creality
if [ ! -d $BACKUP_DIR ]; then
        echo "Backup directory does not exist!"
        exit 1
fi

cp $BACKUP_DIR/S12boot_display /etc/init.d/S12boot_display
cp $BACKUP_DIR/S50dropbear /etc/init.d/S50dropbear
cp $BACKUP_DIR/S99start_app /etc/init.d/S99start_app
mv /usr/bin/Monitor.disable /usr/bin/Monitor
mv /usr/bin/display-server.disable /usr/bin/display-server
rm /etc/init.d/S99guppyscreen

killall guppyscreen

read -p "Do you want to delete the BackupDir at ? (y/n): " delete

if [ $delete == "y" ]
then
        echo "Okay I will delete the BackupDir"
        rm -rf $BACKUP_DIR
else
        echo "Okay I wont delete the BackupDir"
fi

/etc/init.d/S99start_app start &> /dev/null
