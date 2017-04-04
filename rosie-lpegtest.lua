rosie = require "rosie"
lpeg = rosie._env.lpeg
json = rosie._env.json
load_module = rosie._env.load_module
termcolor = load_module("termcolor", "submodules/lua-modules")
test = load_module("test", "submodules/lua-modules")
heading, subheading, check = test.heading, test.subheading, test.check

test.start()

foo = lpeg.rcap((lpeg.R("09")^0), "foo")
foos = lpeg.rcap((foo * (lpeg.P" " * foo)^0), "many foos")
bar = lpeg.P(1) * lpeg.rcap(((lpeg.P"X" * foos)^1) * lpeg.P"abc", "bar")

function check_table(t, typename, s, e, subs)
   check(type(t)=="table", nil, 1)
   if type(t)=="table" then
      check(type(t.s)=="number", nil, 1)
      if s and type(t.s)=="number" then check(t.s==s); end
      check(type(t.e)=="number", nil, 1)
      if e and type(t.e)=="number" then check(t.e==e); end
      check(type(t.type)=="string", nil, 1)
      if typename and t.type then check(t.type==typename, nil, 1); end
      if subs then
	 if subs ~= 0 then
	    check(type(t.subs)=="table", nil, 1);
	    if type(t.subs=="table") then check(#t.subs==subs, nil ,1); end
	 elseif subs==0 then check(not t.subs, nil, 1); end
      end
   end
end

heading("MISC")

s, msg = bar:rmatch("kX7 3 2X1 8abcdef", 1, 99)
check(type(s)=="nil")
check(type(msg)=="string")
if type(msg)=="string" then check(msg:find("invalid encoding")); end


heading("JSON")

subheading("Captures using indices")

s = bar:rmatch("kX7 3 2X1 8abcdef", 1, 1, 0, 0)	    -- "json"
check(type(s)=="userdata")
t = json.decode(lpeg.getdata(s))
check_table(t, "bar", 2, 15, 2)
check(#t.subs==2)
for i,v in ipairs(t.subs) do check_table(v, "many foos"); end

s = bar:rmatch("kX7 X1 8 X9 X101 102 103 104abcdef", 1, 1, 0, 0) -- "json"
check(type(s)=="userdata")
t = json.decode(lpeg.getdata(s))
check_table(t, "bar", 2, 32, 4)
check(t.s==2)
check(t.e==32)
check(#t.subs==4)
for i,v in ipairs(t.subs) do
   check_table(v, "many foos");
   check(#v.subs > 0)
   for i,v in ipairs(v.subs) do
      check_table(v, "foo")
      check(not v.subs)
   end
end

s = bar:rmatch("kX X Xabcdef", 1, 1, 0, 0) -- "json"
check(type(s)=="userdata")
t = json.decode(lpeg.getdata(s))
check_table(t)
check(t.s==2)
check(t.e==10)
check(#t.subs==3)
for i,v in ipairs(t.subs) do
   check_table(v, "many foos");
   check(#v.subs > 0)
   for i,v in ipairs(v.subs) do
      check_table(v, "foo")
      check(not v.subs)
      check(v.s==v.e)				    -- these foos are empty
   end
end

subheading("Aliasing")

foo = lpeg.rcap((lpeg.R("09")^0), "foo")
foos = (foo * (lpeg.P" " * foo)^0)		    -- no rcap
bar = lpeg.P(1) * lpeg.rcap(((lpeg.P"X" * foos)^1) * lpeg.P"abc", "bar")

s = bar:rmatch("kX7 3 2X1 8abcdef", 1, 1, 0, 0) -- "json"
check(type(s)=="userdata")
t = json.decode(lpeg.getdata(s))
check_table(t, "bar")
check(t.s==2)
check(t.e==15)
check(#t.subs==5)
for i,v in ipairs(t.subs) do
   check_table(v, "foo");
   check(not v.subs)
end


s = bar:rmatch("kX7 X1 8 X9 X101 102 103 104abcdef", 1, 1, 0, 0)
check(type(s)=="userdata")
t = json.decode(lpeg.getdata(s))
check_table(t)
check(t.s==2)
check(t.e==32)
check(#t.subs==11)
for i,v in ipairs(t.subs) do
      check_table(v, "foo")
      check(not v.subs)
end

s, err = bar:rmatch("kXabcdef", 1, 1, 0, 0)
check(type(s)=="userdata")
t = json.decode(lpeg.getdata(s))
check_table(t)
check(t.s==2)
check(t.e==6)
check(#t.subs==1)
check(t.subs[1].s==t.subs[1].e)


heading("BYTE ARRAY ENCODING")

foo = lpeg.rcap((lpeg.R("09")^0), "foo")
foos = lpeg.rcap((foo * (lpeg.P" " * foo)^0), "many foos")
bar = lpeg.P(1) * lpeg.rcap(((lpeg.P"X" * foos)^1) * lpeg.P"abc", "bar")

subheading("Captures using indices")

s = bar:rmatch("kX7 3 2X1 8abcdef", 1, 0, 0, 0)	    
check(type(s)=="userdata")
check(#s==116)
check_table(lpeg.decode(s), "bar", 2, 15, 2)

s = bar:rmatch("kX7 X1 8 X9 X101 102 103 104abcdef", 1, 0, 0, 0) 
check(type(s)=="userdata")
check(#s==232)
check_table(lpeg.decode(s), "bar", 2, 32, 4)

s = bar:rmatch("kX X Xabcdef", 1, 0, 0, 0) 
check(type(s)=="userdata")
check(#s==135)
check_table(lpeg.decode(s), "bar", 2, 10, 3)
for i,v in ipairs(t.subs) do
   check_table(v, "foo", nil, nil, 0)
   check(v.s==v.e)				    -- these foos are empty
end

subheading("Aliasing")

foo = lpeg.rcap((lpeg.R("09")^0), "foo")
foos = (foo * (lpeg.P" " * foo)^0)		    -- NO NAME, NO CALL TO r_capindices
bar = lpeg.P(1) * lpeg.rcap(((lpeg.P"X" * foos)^1) * lpeg.P"abc", "bar")

s = bar:rmatch("kX7 3 2X1 8abcdef", 1, 0, 0, 0) 
check(type(s)=="userdata")
check(#s==78)
check_table(lpeg.decode(s), "bar", 2, 15, 5)
for i,v in ipairs(t.subs) do
   check_table(v, "foo", nil, nil, 0);
end

s = bar:rmatch("kX7 X1 8 X9 X101 102 103 104abcdef", 1, 0, 0, 0)
check(type(s)=="userdata")
check(#s==156)
check_table(lpeg.decode(s), "bar", 2, 32, 11)
for i,v in ipairs(t.subs) do
      check_table(v, "foo", nil, nil, 0)
end

s, err = bar:rmatch("kXabcdef", 1, 0, 0, 0)
check(type(s)=="userdata")
check(#s==26)
check_table(lpeg.decode(s), "bar", 2, 6, 1)
check(t.subs[1].s==t.subs[1].e)

heading("Long patterns") -- these generate an open/close instead of a full capture
txt = string.rep("abcdefghij", 30)
bar = lpeg.rcap(lpeg.P(txt), "BAR")
s, l = bar:rmatch(txt, 1)
ok, t = pcall(lpeg.decode, s)
check(ok)
check_table(t, "BAR")
check(t.e==301)
check(t.s==1)

s, l = bar:rmatch(txt, 1, 1)
ok, t = pcall(json.decode, lpeg.getdata(s))
check_table(t, "BAR", 1, 301)


heading("UNHANDLED CAPTURE TYPES")

foo = lpeg.rcap((lpeg.R("09")^0), "foo")
foos = lpeg.rcap((foo * (lpeg.P" " * foo)^0), "many foos")

subheading("Accidental use of match instead of rmatch")

s, last = foos:match("123 4 567890")
check(type(s)=="nil")
check(type(last)=="string")
check(last:find("invalid capture type"))


subheading("Match types other than Crosiecap when using rmatch")

-- First check that these patterns work, before we introduce an error
foo = lpeg.rcap((lpeg.R("09")^0), "foo")
foos = lpeg.rcap((foo * (lpeg.P" " * foo)^0), "many foos")
s, last = foos:rmatch("123 4 567890")
check(type(s)=="userdata")
check(type(last)=="number")

function check_error(pat, input, msg)
   s, last = pat:rmatch(input)
   check(type(s)=="nil", nil, 1)
   check(type(last)=="string", nil, 1)
   check(last:find(msg), "unexpected error message: " .. tostring(last), 1)
end

foos = lpeg.rcap((foo * (lpeg.P" " * lpeg.Cc("PROBLEM") * foo)^0), "many foos")
check_error(foos, "123 4 567890", "full capture error")

foos = (foo * (lpeg.P" " * lpeg.Cc("PROBLEM") * foo)^0)
check_error(foos, "123 4 567890", "open capture error")


heading("Grammars")

-- Equal numbers of a's and b's
equalcount = lpeg.C(lpeg.P{
  "S";   -- initial rule name
  S = "a" * lpeg.V"B" + "b" * lpeg.V"A" + "",
  A = "a" * lpeg.V"S" + "b" * lpeg.V"A" * lpeg.V"A",
  B = "b" * lpeg.V"S" + "a" * lpeg.V"B" * lpeg.V"B",
} * -1) * lpeg.Cp()

bal = "aabb"
s, n = equalcount:match(bal)
check(n==5, "grammar without using rosie lpeg extensions")
check(s==bal)

-- Entire grammar inside rcap
equalcount = lpeg.rcap(
   lpeg.P{
      "S";   -- initial rule name
      S = "a" * lpeg.V"B" + "b" * lpeg.V"A" + "",
      A = "a" * lpeg.V"S" + "b" * lpeg.V"A" * lpeg.V"A",
      B = "b" * lpeg.V"S" + "a" * lpeg.V"B" * lpeg.V"B",
   } * -1,
   "S")

s, n = equalcount:rmatch(bal)
check(type(s)=="userdata")
t = lpeg.decode(s)
check_table(t, "S", 1, 5)

equalcount = 
   lpeg.P{
      "S";   -- initial rule name
      S = lpeg.rcap("a" * lpeg.V"B" + "b" * lpeg.V"A" + "", "S"),
      A = lpeg.rcap("a" * lpeg.V"S" + "b" * lpeg.V"A" * lpeg.V"A", "A"),
      B = lpeg.rcap("b" * lpeg.V"S" + "a" * lpeg.V"B" * lpeg.V"B", "B")
   } * -1


s, n = equalcount:rmatch(bal)
check(type(s)=="userdata")
t = lpeg.decode(s)
check_table(t, "S", 1, 5, 1)
check_table(t.subs[1], "B", 2, 5, 2)

bal = "baab"
s, n = equalcount:rmatch(bal)
t = lpeg.decode(s)
check_table(t, "S", 1, 5, 1)
check_table(t.subs[1], "A", 2, 5, 1)


test.finish()


