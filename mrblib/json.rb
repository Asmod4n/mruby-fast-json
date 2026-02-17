module JSON
  def self.parse_lazy(json)
    Document.new(json)
  end
end
