.PHONY: build clean test lint install uninstall install-deps certs

# Serial-ish default: unbounded -j OOMs on the Pi Zero 2 W (512 MB) during
# OpenCV-heavy compiles. Override on bigger machines: make build JOBS=8
JOBS ?= 2

build:
	mkdir -p build
	cd build; cmake ../; make -j$(JOBS)

test:
	mkdir -p build
	cd build; cmake -DBUILD_TESTS=ON ../; make -j$(JOBS) dashcam_tests; ctest --output-on-failure

clean:
	rm -rf build

lint:
	cppcheck --enable=all --suppress=missingIncludeSystem --suppress=unusedFunction -I src/ -I include/ src/ include/ run/ tests/

install:
	$(MAKE) build
	sudo cp build/dashcam /usr/local/bin/dashcam
	./systemd/configure-service.sh
	sudo cp systemd/dashcam.service /etc/systemd/system/dashcam.service
	mkdir -p /home/${USER}/.dashcam
	[ -f /home/${USER}/.dashcam/dashcam.conf ] || cp ./dashcam.conf.example /home/${USER}/.dashcam/dashcam.conf
	sudo systemctl daemon-reload
	sudo systemctl enable --now dashcam.service

# Mirrors install: stops, disables, and removes the unit and the binary.
# Leaves ~/.dashcam (config, videos, manifest) untouched — uninstalling the
# service must never delete recordings.
uninstall:
	sudo systemctl disable --now dashcam.service || true
	sudo rm -f /etc/systemd/system/dashcam.service
	sudo systemctl daemon-reload
	sudo rm -f /usr/local/bin/dashcam

install-deps:
	sudo apt install -y cppcheck build-essential cmake pkg-config git libssl-dev libopencv-dev libcamera-dev libcamera-dev

certs:
	mkdir -p certs
	cd certs; openssl genpkey -algorithm RSA -out key.pem; openssl req -new -key key.pem -out csr.csr -subj "/C=US/ST=VA/L=Purcellville/O=c2/OU=c2/CN=c2"; openssl x509 -req -days 365 -in csr.csr -signkey key.pem -out cert.pem
