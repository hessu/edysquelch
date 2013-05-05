<!--

function lz(i)
{
	if (i < 10)
		return '0' + i;
	
	return i;
}

function timestr(i)
{
	if (i === undefined)
		 return '';
	
	var D = new Date(i*1000);
	return D.getUTCFullYear() + '-' + lz(D.getUTCMonth()+1) + '-' + lz(D.getUTCDate())
		+ ' ' + lz(D.getUTCHours()) + ':' + lz(D.getUTCMinutes()) + ':' + lz(D.getUTCSeconds())
		+ 'z';
}

function dur_str(i)
{
	if (i === undefined)
		 return '';
	
	var t;
	var s = '';
	var c = 0;
	
	if (i > 86400) {
		t = Math.floor(i/86400);
		i -= t*86400;
		s += t + 'd';
		c++;
	}
	if (i > 3600) {
		t = Math.floor(i / 3600);
		i -= t*3600;
		s += t + 'h';
		c++;
	}
	if (c > 1)
		return s;
		
	if (i > 60) {
		t = Math.floor(i / 60);
		i -= t*60;
		s += t + 'm';
		c++;
	}
	
	if (c)
		return s;
	
	return i.toFixed(0) + 's';
}

var evq = {};
var ev = [];

/*
 *	give a go at using AngularJS
 */

var app = angular.module('edysquelch', []).
	config(function() {
		console.log('edy module config');
	}).
	run(function() {
		console.log('edy module run');
	});

app.filter('duration', function() { return dur_str; });
app.filter('datetime', function() { return timestr; });

app.controller('edyCtrl', [ '$scope', '$http', function($scope, $http) {
	console.log('edyCtrl init');
	
	var plotEvent = function(pe) {
		console.log("Plotting event " + pe['t']);
		
		$scope.shownEvent = pe;
		
		if (pe['rxofs'] == undefined)
			pe['rxofs'] = 0;
		
		var rxvals = [];
		var rx = pe['rx'];
		for (var i = 0; i < rx.length; i++) {
			rxvals.push([i-pe['rxofs'], rx[i]]);
		}
		
		var _d = [
			{ label: 'received', data: rxvals }
			
		];
		
		if (pe['fp']) {
			var fpvals = [];
			var fp = pe['fp'];
			for (var i = 0; i < fp.length; i++) {
				fpvals.push([i, fp[i]]);
			}
			_d.push({ label: 'fingerprint', data: fpvals });
		}
		
		var _x_opt = {
		};
		
		var _y_opt = {
		};
		
		var _mark = [
		];
		
		if (pe['lnoise']) {
			console.log("rxofs " + pe['rxofs'] + " plotting lnoise marker at " + pe['lnoise']);
			_mark.push({
				color: '#2db543', lineWidth: 1, xaxis: { from: pe['lnoise'] - pe['rxofs'], to: pe['lnoise'] - pe['rxofs'] }
			});
		}
		
		var _o = {
			grid: { hoverable: true, autoHighlight: false, minBorderMargin: 20, markings: _mark },
			legend: { position: 'nw' },
			colors: [ '#ff0000', '#0000ff' ],
			xaxis: _x_opt,
			yaxis: _y_opt,
			selection: { mode: "x" }
		};
		
		var elem = $('#graph');
		var pl = $.plot(elem, _d, _o);
		
		/* selection event handlers */
		elem.bind("plotselected", function (event, ranges) {
			console.log("plotselected");
			var to = parseInt(ranges.xaxis.to.toFixed(0));
			var from = parseInt(ranges.xaxis.from.toFixed(0));
			$scope.rangeSelected = {
				'from': from,
				'to': to,
				'ms': ((to-from) / 48000 * 1000).toFixed(3)
			};
			$scope.$apply();
		});
		
		elem.bind("plotunselected", function (event, ranges) {
			console.log("plotunselected");
			$scope.rangeSelected = undefined;
			$scope.$apply();
		});
		
		$scope.rangeZoom = function(range) {
			console.log("rangeZoom");
			
			var opt = pl.getOptions();
			
			console.log("opt: " + JSON.stringify(opt.xaxis));
			
			if (range) {
				_o.xaxis.min = range.from;
				_o.xaxis.max = range.to;
			} else {
				_o.xaxis.min = null;
				_o.xaxis.max = null;
			}
			
			console.log("opt: " + JSON.stringify(_o));
			
			pl = $.plot(elem, _d, _o);
		};
		
		$scope.makeFingerprint = function(range) {
			console.log("makeFingerprint");
			alert('Sorry, we are not quite there yet.');
		};
	};
	$scope.rowClick = plotEvent;
	$scope.ev = ev;
	
	var ajax_update = function($scope, $http) {
		var config = {
			'params': {}
		};
		
		if (evq['seq'] > 0) {
			config['params']['seq'] = evq['seq'];
		}
		
		$http.get('/api/upd', config).success(function(d) {
			console.log('HTTP update received, status: ' + d['result']);
			
			$scope.evq = evq = d['evq'];
			
			if (d['ev']) {
				for (var i in d['ev'])
					ev.unshift(d['ev'][i]);
				
				plotEvent(ev[0]);
			}
			
			setTimeout(function() { ajax_update($scope, $http); }, 1200);
		}).error(function(data, status, headers, config) {
			console.log('HTTP update failed, status: ' + status);
			setTimeout(function() { ajax_update($scope, $http); }, 1200);
		});
	};
	
	ajax_update($scope, $http);
}]);


//-->
