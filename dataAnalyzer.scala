val lf = (loc:String) => io.Source.fromFile(loc).getLines
val debugLoc = """C:\Users\Karl\Documents\GitHub\VideoWatcher\debug.txt"""
val allData = lf(debugLoc).toList
val numData = allData.filter( _.head == '[' )
val data = numData.filter( x=> {
	val s = x.indexOf('[')
	val e = x.indexOf(']')
	e-s > 3 } )
val coords = data.map( x=> x.take( x.indexOf(']')+1 ).trim ).distinct
coords.map( x => {
	data.filter( y => y.take(x.size) == x ).map(_.drop(x.size) ) } )
