package jeash.errors;


class ArgumentError extends Error
{
	public function new(inMessage:String = "")
	{
		super(inMessage);
	}
}
