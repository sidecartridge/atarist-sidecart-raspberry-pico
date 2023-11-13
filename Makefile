# Version from file
VERSION := $(shell cat version.txt)
VERSION_BETA := $(shell cat version-beta.txt)

## Tag this version
.PHONY: tag
tag:
	git tag $(VERSION) && git push origin $(VERSION) && \
	echo "Tagged: $(VERSION)" && \
	python update_version.py

## Tag BETA this version
.PHONY: tag-beta
tag-beta:
	git tag $(VERSION_BETA) && git push origin $(VERSION_BETA) && \
	echo "Tagged: $(VERSION_BETA)" && \
	python update_version_beta.py
