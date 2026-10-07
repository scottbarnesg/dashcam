.PHONY: build clean lint install install-deps certs

build:
	mkdir -p build
	cd build; cmake ../; make -j

clean:
	rm -rf build

lint:
	cppcheck --enable=all --suppress=missingIncludeSystem -I src/ -I include/ src/ include/ run/

install:
	make build
	sudo cp build/dashcam /usr/local/bin/dashcam
	cp ./systemd/dashcam-base.service ./systemd/dashcam.service
	./systemd/configure-service.sh
	sudo cp systemd/dashcam.service /etc/systemd/system/
	mkdir -p /home/${USER}/.dashcam


install-deps:
	sudo apt install -y cppcheck build-essential cmake pkg-config git libssl-dev libopencv-dev

certs:
	mkdir -p certs
	cd certs; openssl genpkey -algorithm RSA -out key.pem; openssl req -new -key key.pem -out csr.csr -subj "/C=US/ST=VA/L=Purcellville/O=c2/OU=c2/CN=c2"; openssl x509 -req -days 365 -in csr.csr -signkey key.pem -out cert.pem
