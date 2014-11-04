#! /opt/local/bin/ruby

require 'optparse'

fbcodeDir = ENV['FBCODE_DIR']
installDir = ENV['INSTALL_DIR']
proxygenHttpPath = File.join(fbcodeDir, "proxygen", "lib", "http")
useInstalledGperf = ENV['INSTALLED_GPERF']

headerFiles = []
OptionParser.new do |opts|
  opts.banner = "Usage: example.rb [options]"

  # This is modestly fragile because there's no simple way to ignore arguments
  # you don't care about
  opts.on("-I", "--install_dir DIR" "Install dir") do |f|
  end
  opts.on("-F", "--fbcode_dir DIR" "fbcode dir") do |f|
  end
  opts.on("-H", "--custom_header FILE", "Additional header files") do |f|
    headerFiles.push(File.join(fbcodeDir, f))
  end
end.parse!

# 1. HTTPCommonHeaders

# This script uses the list in HTTPCommonHeaders.txt to generate
# HTTPCommonHeaders.h from HTTPCommonHeaders.template.h,
# HTTPCommonHeaders.gperf from HTTPCommonHeaders.template.gperf,
# and then HTTPCommonHeaders.cpp from HTTPCommonHeaders.gperf.

# the sources
templateHPath = File.join(proxygenHttpPath, "HTTPCommonHeaders.template.h")
templateGperfPath = File.join(proxygenHttpPath,
                              "HTTPCommonHeaders.template.gperf")
headerNamesListPath = File.join(proxygenHttpPath, "HTTPCommonHeaders.txt")
headerFiles.push(headerNamesListPath)

# the destinations
hPath = File.join(installDir, "HTTPCommonHeaders.h")
gperfPath = File.join(installDir, "HTTPCommonHeaders.gperf")
cppPath = File.join(installDir, "HTTPCommonHeaders.cpp")

headerLines = []
headerFiles.each {|headerFile|
  File.open(headerFile, "r").each_line { |line|
    headerName = line.strip
    next if headerName.empty?
    headerLines.push(headerName)
  }
}
headerLines = headerLines.sort.uniq
insertedContentH = ""
insertedContentGperf = "%%\n"
nextEnumValue = 2
headerLines.each {|headerName|
  enumName = "HTTP_HEADER_" + headerName.gsub('-', '_').upcase
  insertedContentH << "  #{enumName} = #{nextEnumValue},\n"
  insertedContentGperf << "#{headerName}, #{enumName}\n"
  nextEnumValue += 1
}
insertedContentGperf << "%%"

# generate HTTPCommonHeaders.h
f = File.open(hPath, "w")
f.write(
  File.open(templateHPath, "r").read.gsub('%%%%%', insertedContentH)
)
f.close

# generate HTTPCommonHeaders.gperf
f = File.open(gperfPath, "w")
f.write(
  File.open(templateGperfPath, "r").read.gsub('%%%%%', insertedContentGperf)
)
f.close

# and finally run gperf
if useInstalledGperf.nil?
  gperf = File.join(fbcodeDir, "third-party2", "gperf",
                    "3.0.4", "gcc-4.8.1-glibc-2.17-fb",
                    "c3f970a", "bin", "gperf")
else
  gperf = "gperf"
end

system("#{gperf} #{gperfPath} -m 5 --output-file=#{cppPath}")
