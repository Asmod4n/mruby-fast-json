#!/usr/bin/env ruby
require 'json'

File.open("output.json", "w") do |file|
  file.write("[\n")
  size = 0
  i = 0
  buffer = []

  while size < 100 * 1024 * 1024  # 100 MB
    obj = { id: i, meta: { a: 1, b: 2 }, data: "x" * 100_000 }

    json = JSON.dump(obj)
    buffer << json
    size += json.bytesize
    i += 1
  end

  file.write(buffer.join(",\n"))
  file.write("\n]")  # Close JSON array properly
end

puts "Created output.json (approx. 100 MB)"
