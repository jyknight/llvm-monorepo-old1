public class FloatCompare
{
    public static void main(String[] args) {
        int count = 0;

        for (float f = 0.0F; f < 10F; f += 1.1F)
            ++count;

        for (double d = 100; d > 0; d -= 11)
            ++count;

        Test.print_int_ln(count);
    }
}
