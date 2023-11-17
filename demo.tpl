
/*
let x = 4.;

let printit = function(s) {
        let t = "...says I";
        s = s + t;
        print("{1}\n", s);
        return s;
};

let y = printit("Hello" + " " + "world!");
x = 10 - 5.5;
print("x is {1}\n", x);
print("Return value of printit was '{}'\n", y);
print("typeof return value is `{}'\n", typeof(y));

*/

let pow = function(x, y) {
    if (typeof(x) != "int" || typeof(y) != "int") {
        print("wrong type!\n");
        return 0.0;
    }
    if (y <= 0) {
        // these are ints, so negative powers always
        // evaluate to zero.
        return 0;
    }
    let res = x;
    while (y > 1) {
        res = res * x;
        y = y - 1;
    }
    return res;
};

let iterator = {
        tonine: function(x) {
                while (x < 10) {
                        print("x is {}\n".format(x));
                        x = x + 1;
                }
        },
        toten: function(x) {
                while (x < 11) {
                        print("{}\n".format(x));
                        x = x + 1;
                }
        }
};

iterator.whoami = function() {
    print("me, of course!\n");
};

let toten = function(x) {
        while (x < 11) {
                print("{}\n".format(x));
                x = x + 1;
        }
};

iterator.tonine(4);
print("That was to nine. This is to ten:\n");
iterator.toten(4);
print("4^4 = {}\n".format(pow(4, 4)));
iterator.whoami();

let s = "This is a string";
print("length '{}' for '{}'\n".format(s.len(), s));

// : vim: set syntax=javascript :
