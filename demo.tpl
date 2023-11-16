
let x = 4.;

let printit = function(s) {
        let t = "...says I";
        s = s + t;
        PRINT("{1}\n", s);
        return s;
};

let y = printit("Hello" + " " + "world!");
x = 10 - 5.5;
PRINT("x is {1}\n", x);
PRINT("Return value of printit was '{}'\n", y);
PRINT("typeof return value is `{}'\n", typeof(y));

