
history={}
table.setn(history, 20)

t = tracker.new(20, 40)

features={}
average={}

function features.add(self, idx, x, y)
   average.x = average.x + x
   average.y = average.y + y
   average.count = average.count + 1

   self[idx] = {x=x, y=y}
end

star = gfx.texture('star-la.png')
blob = gfx.texture('blob.png')

function process_frame(frame)
   average={x=0, y=0, count=0}
   t:track(features)
   average.x = average.x / average.count
   average.y = average.y / average.count

   --print('delta=',average.x, average.y)

   table.insert(history, 1, frame)
   table.remove(history)

   gfx.setstate({blend='add'})

   for i in history do
      local f = history[i]

      if f then
	 local b = .2 / i
	 gfx.setstate({blend='add', colour={b, b, b}})
	 gfx.sprite({x=320/2, y=240/2}, f.width * (1 + (i-1)*.05), f)
      end
   end

   gfx.setstate({blend='alpha', colour={.5,.5,.2,1}})
   for i in features do
      if type(i) == 'number' then
	 gfx.sprite(features[i], 24, star)
      end
   end

   --print('gc=',gcinfo())
end
