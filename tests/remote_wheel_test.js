/* Exercise the actual viewer handler; Node's standard library only. */
const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm'),path=require('node:path');
const source=fs.readFileSync(path.join(__dirname,'../assets/remote_display.html'),'utf8').match(/<script>([\s\S]*?)<\/script>/)[1];
const handlers={},requests=[];
const screen={getContext:()=>({}),getBoundingClientRect:()=>({left:0,top:0,width:1024,height:600}),addEventListener:(kind,handler)=>handlers[kind]=handler};
const document={getElementById:id=>id==='screen'?screen:{addEventListener:()=>{}}};
const sandbox={document,setTimeout:()=>{},fetch:async(url,options)=>{
 if(url==='/pointer'){requests.push(JSON.parse(options.body));return {ok:true,json:async()=>({accepted:true})};}
 return {json:async()=>({frame:0,connected:true})};
}};
vm.createContext(sandbox);vm.runInContext(source,sandbox);
const wheel=(x,y,deltaY,deltaMode=0)=>handlers.wheel({clientX:x,clientY:y,deltaY,deltaMode,preventDefault:()=>{}});
const settled=()=>new Promise(resolve=>setImmediate(resolve));
(async()=>{
 for(let i=0;i<4;i++)wheel(350,165,3); /* A TTS-button hover must not become a tap. */
 assert.equal(requests.length,0);wheel(350,165,3);await settled();
 assert.equal(requests.length,1);assert.equal(requests[0].press_y-requests[0].y,15);
 wheel(600,300,1,1);await settled();assert.equal(requests.at(-1).press_y-requests.at(-1).y,16);
 wheel(600,300,-1,2);await settled();assert.equal(requests.at(-1).y-requests.at(-1).press_y,200);
 vm.runInContext('wheelPixels=0',sandbox);
 const count=requests.length;wheel(350,0,50);await settled();assert.equal(requests.length,count);
 for(const request of requests)assert.ok(Math.abs(request.y-request.press_y)>12);
 console.log('PASS_REMOTE_WHEEL small deltas accumulate, line/page units, no wheel taps at buttons or edges');
})().catch(error=>{console.error(error);process.exitCode=1;});
