sub f {
    return $_[0] + $_[1] + $_[2];
}

for (my $i = 0; $i < 2500000; $i++) {
    f(1, 2, 3);
}