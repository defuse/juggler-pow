B = 5
S = 4
M = B + S

sum = 0.0
1.upto(2**S) do |m|
  hit_count = 2**S - m
  vala = (2**B - 1)**(2**M - hit_count) * 2**B
  valb = 2**( B*(hit_count) + B*(2**M - hit_count) )
  puts "#{vala.to_f/valb}"
end
puts sum

