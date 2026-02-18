module JSON
  class << self
    attr_accessor :zero_copy_parsing
    def parse_lazy(json)
      Document.new(json)
    end
  end
end
