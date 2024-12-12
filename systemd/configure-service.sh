sed -i "/^User=/ s/$/$USER\n/" systemd/dashcam.service
echo "WorkingDirectory=/home/$USER/.dashcam" >> systemd/dashcam.service