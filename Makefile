# Version from file
VERSION := $(shell cat version.txt)

## Tag this version
.PHONY: tag
tag:
	git tag v$(VERSION) && git push origin v$(VERSION) && \
	echo "Tagged: $(VERSION)" && \
	python update_version.py
