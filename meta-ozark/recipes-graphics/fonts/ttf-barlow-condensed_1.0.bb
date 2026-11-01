SUMMARY = "Barlow Condensed — the Ozark brand typeface"
DESCRIPTION = "Barlow Condensed Medium, SemiBold and Bold, as used in the \
Ozark Mountain brand. Vendored from the Google Fonts repository \
(commit 9862abc066c9b83bf84c3e8ed1fc2d8d48f9bbf0, ofl/barlowcondensed)."
HOMEPAGE = "https://github.com/jpt/barlow"
SECTION = "fonts"
LICENSE = "OFL-1.1"
LIC_FILES_CHKSUM = "file://OFL.txt;md5=46cbde1f44ff31807ec209f1b72acad2"

SRC_URI = " \
    file://BarlowCondensed-Medium.ttf \
    file://BarlowCondensed-SemiBold.ttf \
    file://BarlowCondensed-Bold.ttf \
    file://OFL.txt \
"

S = "${WORKDIR}"

inherit allarch fontcache

do_install() {
    install -d ${D}${datadir}/fonts/truetype/barlow-condensed
    install -m 0644 ${S}/*.ttf ${D}${datadir}/fonts/truetype/barlow-condensed/
}

FILES:${PN} = "${datadir}/fonts"
