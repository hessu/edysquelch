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

var ajax_update = function($scope, $http)
{
	var config = {
		'params': {}
	};
	
	if (evq['seq'] > 0) {
		config['params']['seq'] = evq['seq'];
	}
	
	$http.get('/api/upd', config).success(function(d) {
		console.log('NG got status: ' + d['result']);
		
		$scope.evq = evq = d['evq'];
		
		for (var i in d['ev']) {
			ev.unshift(d['ev'][i]);
		}
		
		//$scope.ev = ev;
		
		setTimeout(function() { ajax_update($scope, $http); }, 1200);
	});
}

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
	
	$scope.rowClick = function(pe) {
		console.log("row click for " + pe['t']);
		
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
		
		var _o = {
			grid: { hoverable: true, autoHighlight: false, minBorderMargin: 20 },
			legend: { position: 'nw' },
			colors: [ '#ff0000', '#0000ff' ],
			xaxis: _x_opt,
			yaxis: _y_opt
		};
		
		$.plot($('#graph'), _d, _o);
	};
	
	$scope.ev = ev;
	
	ajax_update($scope, $http);
}]);


//-->
