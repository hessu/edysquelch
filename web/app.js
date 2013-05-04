
var http = require('http'),
	express = require("express"),
	util = require("util"),
	events = require("events");
                
util.log("startup");

var evq_seq = 0;
var evq_len = 0;
var evq = [];

var app = express();
app.configure(function() {
	app.use(express.bodyParser());
	app.use(express.methodOverride());
	app.use(app.router);
	app.use(express.static("static"));
	app.use(express.errorHandler({ dumpExceptions: true, showStack: true }));
});

var emitter = new events.EventEmitter;

var handle_push = function(req, res) {
	util.log("got push");
	
	if (req.body['what'] != 'edy') {
		res.json({'result': 'fail'});
		return;
	}
	
	var c = req.body['c'] * 1;
	for (var i = 0; i < c; i++) {
		var j = req.body[i];
		util.log("decoding: " + j);
		var m = JSON.parse(j);
		evq.push(m);
		evq_len++;
		evq_seq++;
		util.log("got new event " + evq_seq + " evq len " + evq_len);
	}
	
	while (evq_len > 10) {
		evq.shift();
		evq_len--;
		util.log("expired from evq, len now " + evq.length + " / " + evq_len)
	}
	
	console.log("event listener count: " + util.inspect(emitter.listeners('event:notify')));
	emitter.emit("event:notify", evq_seq);
	
	res.json({'result': 'ok'});
};

function last_events(n)
{
	var a = [];
	var l = evq.length;
	for (var i = l - n; i < l; i++)
		a.push(evq[i]);
	
	util.log("last_events returning " + a.length + " events");
	
	return a;
}

function upd_response(seq, res)
{
	var seq_dif = evq_seq - seq;
	util.log("client is " + seq_dif + " events late");
	
	var ev;
	if (seq_dif > 0)
		ev = last_events(seq_dif);
	else
		ev = [];
	
	res.setHeader('Cache-Control', 'no-cache');
	res.json({
		'result': 'ok',
		'evq': {
			'seq': evq_seq,
			'len': evq_len
		},
		'ev': ev
	});
}

var handle_upd = function(req, res) {
	util.log("got upd req: " + JSON.stringify(req.query));
	
	var seq = parseInt(req.query['seq']);
	if (seq) {
		util.log("updating with seq " + seq);
	} else {
		if (evq_seq > 0)
			seq = evq_seq - ((evq_len > 10) ? 10 : evq_len);
		else
			seq = 0;
		
		util.log("starting with seq " + seq);
	}
	
	var seq_dif = evq_seq - seq;
	
	if (seq_dif == 0) {
		util.log("going longpoll");
		// handler function
		var notify = function(id) {
			util.log("sending longpoll response, seq now " + id);
			upd_response(seq, res);
		};
		// when we have an event, return response
		emitter.once("event:notify", notify);
		// if the client closes, remove listener
		req.on("close", function() { util.log("client closed in middle of longpoll"); emitter.removeListener("event:notify", notify); });
		return;
	}
	
	upd_response(seq, res);
};

app.post('/api/push', handle_push); 
app.get('/api/upd', handle_upd); 

util.log("pwm-api set up, starting listener");

app.listen(8666);


