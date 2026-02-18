module JSON
  class << self
    attr_accessor :zero_copy_parsing

    def load_lazy(source)
      Document.new(source, load: true)
    end
  end
end
