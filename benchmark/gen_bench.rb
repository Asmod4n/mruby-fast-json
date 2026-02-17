require 'json'

File.open("output.json", "w") do |file|
  file.write("[\n")
  size = 0
  i = 0
  buffer = []

  while size < 10 * 1024 * 1024  # 10 MB
    obj = { id: i, name: "Item #{i}", data: "x" * 100 }
    json = JSON.dump(obj)
    buffer << json
    size += json.bytesize + 2  # account for comma + newline
    i += 1
  end

  file.write(buffer.join(",\n"))
  file.write("\n]")  # Close JSON array properly
end

puts "Created output.json (approx. 10 MB)"
