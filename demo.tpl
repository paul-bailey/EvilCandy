

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
        for (let n = 10; x < n; x++)
                print("\titerator.tonine: x is {}\n".format(x));
    },

    toten: function(x) {
        let n = 11;
        do {
            print("\titerator.toten: x is {}\n".format(x));
            x++;
        } while (x < n);
    }
};

iterator.whoami = function() {
    print("me, of course!\n");
};

iterator.tonine(4);
print("That was to nine. This is to ten:\n");
iterator.toten(4);
print("4^4 = {}\n".format(pow(4, 4)));
print("Who am I?\n");
iterator.whoami();

let s = "This is a string";
print("length '{}' for '{}'\n".format(s.len(), s));
print("iterator.len()=={}\n".format(iterator.len()));
print("length {} should equal {}\n".format(s.len(), len(s)));
print("Length of __gbl__ is {}\n".format(len()));

print("\n");
print("~0xfffffffffffffffa is {}\n".format(~0xfffffffffffffffa));

print("\n");
print("The 3rd character in '{}' is '{}'\n".format(s, s[2]));

print("\n");
print("Now here's a test of foreach:\n");
print("I'm gonna print the typeof(e) for each e in __gbl__\n");
__gbl__.foreach(function (e) {
    print("\t" + typeof(e) + "\n");
});

// : vim: set syntax=javascript :
